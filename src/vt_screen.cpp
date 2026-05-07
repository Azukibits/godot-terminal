#include "vt_screen.h"

extern "C" {
#include "vterm.h"
#include "vterm_keycodes.h"
}

#include <algorithm>
#include <cstring>

namespace godot {

namespace {

// libvterm gives integer-valued indexed colors. For cells flagged
// DEFAULT_FG/BG we substitute the user-configured Terminal colors instead of
// libvterm's palette default (gray/black) so it looks like the user expects.
inline Color color_from_vterm(const VTermColor &c, const VTermScreen *screen,
                              const Color &default_fg, const Color &default_bg,
                              bool is_fg) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&c)) return default_fg;
    if (VTERM_COLOR_IS_DEFAULT_BG(&c)) return default_bg;

    VTermColor rgb = c;
    if (!VTERM_COLOR_IS_RGB(&rgb)) {
        // Convert indexed (16-color or 256-color) into RGB using libvterm's palette.
        vterm_screen_convert_color_to_rgb(screen, &rgb);
    }
    if (VTERM_COLOR_IS_RGB(&rgb)) {
        return Color(rgb.rgb.red / 255.0f, rgb.rgb.green / 255.0f,
                     rgb.rgb.blue / 255.0f);
    }
    return is_fg ? default_fg : default_bg;
}

// Shared by VTScreen::read_cell and the sb_pushline trampoline.
inline void convert_cell(const VTermScreenCell &c, const VTermScreen *screen,
                         const Color &default_fg, const Color &default_bg,
                         VTRenderCell &out) {
    out = VTRenderCell{};
    out.width = c.width;
    out.ch = c.chars[0] != 0 ? static_cast<char32_t>(c.chars[0]) : U' ';
    out.bold = c.attrs.bold != 0;
    out.italic = c.attrs.italic != 0;
    out.underline = c.attrs.underline != 0;
    out.reverse_video = c.attrs.reverse != 0;

    Color fg = color_from_vterm(c.fg, screen, default_fg, default_bg, true);
    Color bg = color_from_vterm(c.bg, screen, default_fg, default_bg, false);

    if (out.reverse_video) {
        std::swap(fg, bg);
        bg.a = 1.0f;
    } else if (VTERM_COLOR_IS_DEFAULT_BG(&c.bg)) {
        bg.a = 0.0f;
    }
    out.fg = fg;
    out.bg = bg;
}

// --- C → C++ trampolines ----------------------------------------------------
int cb_damage(VTermRect /*rect*/, void *user) {
    static_cast<VTScreen *>(user)->_on_damage();
    return 1;
}
int cb_moverect(VTermRect /*dest*/, VTermRect /*src*/, void *user) {
    static_cast<VTScreen *>(user)->_on_damage();
    return 1;
}
int cb_movecursor(VTermPos pos, VTermPos /*oldpos*/, int visible, void *user) {
    static_cast<VTScreen *>(user)->_on_movecursor(pos.col, pos.row, visible != 0);
    return 1;
}
int cb_settermprop(VTermProp /*prop*/, VTermValue * /*val*/, void * /*user*/) {
    // Ignore for Phase 3; Phase 6 will surface title / cursor-shape.
    return 1;
}
int cb_bell(void * /*user*/) {
    return 1;
}
int cb_resize(int rows, int cols, void *user) {
    static_cast<VTScreen *>(user)->_on_resize(cols, rows);
    return 1;
}
int cb_sb_pushline(int cols, const VTermScreenCell *cells, void *user) {
    auto *self = static_cast<VTScreen *>(user);
    self->_pushline_from_libvterm(cols, cells);
    return 1;
}
int cb_sb_popline(int /*cols*/, VTermScreenCell * /*cells*/, void * /*user*/) {
    return 0;
}
int cb_sb_clear(void * /*user*/) {
    return 1;
}

void cb_output(const char *bytes, size_t len, void *user) {
    static_cast<VTScreen *>(user)->_on_output(bytes, len);
}

VTermKey to_vterm_key(int k) {
    switch (k) {
        case VT_KEY_ENTER:     return VTERM_KEY_ENTER;
        case VT_KEY_TAB:       return VTERM_KEY_TAB;
        case VT_KEY_BACKSPACE: return VTERM_KEY_BACKSPACE;
        case VT_KEY_ESCAPE:    return VTERM_KEY_ESCAPE;
        case VT_KEY_UP:        return VTERM_KEY_UP;
        case VT_KEY_DOWN:      return VTERM_KEY_DOWN;
        case VT_KEY_LEFT:      return VTERM_KEY_LEFT;
        case VT_KEY_RIGHT:     return VTERM_KEY_RIGHT;
        case VT_KEY_INS:       return VTERM_KEY_INS;
        case VT_KEY_DEL:       return VTERM_KEY_DEL;
        case VT_KEY_HOME:      return VTERM_KEY_HOME;
        case VT_KEY_END:       return VTERM_KEY_END;
        case VT_KEY_PAGEUP:    return VTERM_KEY_PAGEUP;
        case VT_KEY_PAGEDOWN:  return VTERM_KEY_PAGEDOWN;
        case VT_KEY_KP_ENTER:  return VTERM_KEY_KP_ENTER;
        default:
            if (k >= VT_KEY_F1 && k <= VT_KEY_F1 + 11) {
                int n = k - VT_KEY_F1 + 1;
                return static_cast<VTermKey>(VTERM_KEY_FUNCTION(n));
            }
            return VTERM_KEY_NONE;
    }
}

const VTermScreenCallbacks kCallbacks = {
    /*damage     */ &cb_damage,
    /*moverect   */ &cb_moverect,
    /*movecursor */ &cb_movecursor,
    /*settermprop*/ &cb_settermprop,
    /*bell       */ &cb_bell,
    /*resize     */ &cb_resize,
    /*sb_pushline*/ &cb_sb_pushline,
    /*sb_popline */ &cb_sb_popline,
    /*sb_clear   */ &cb_sb_clear,
    /*sb_pushline4*/ nullptr,
};

} // namespace

// ----------------------------------------------------------------------------

VTScreen::VTScreen(int cols, int rows) : cols_(std::max(cols, 1)), rows_(std::max(rows, 1)) {
    vt_ = vterm_new(rows_, cols_);
    vterm_set_utf8(vt_, 1);
    vterm_output_set_callback(vt_, &cb_output, this);

    screen_ = vterm_obtain_screen(vt_);
    vterm_screen_set_callbacks(screen_, &kCallbacks, this);
    vterm_screen_reset(screen_, /*hard*/ 1);
    _apply_default_colors();
}

VTScreen::~VTScreen() {
    if (vt_ != nullptr) {
        vterm_free(vt_);
        vt_ = nullptr;
        screen_ = nullptr;
    }
}

void VTScreen::feed(const char *bytes, std::size_t len) {
    if (vt_ == nullptr || bytes == nullptr || len == 0) return;
    vterm_input_write(vt_, bytes, len);
    vterm_screen_flush_damage(screen_);
}

void VTScreen::resize(int cols, int rows) {
    cols = std::max(cols, 1);
    rows = std::max(rows, 1);
    if (cols == cols_ && rows == rows_) return;
    vterm_set_size(vt_, rows, cols);
    cols_ = cols;
    rows_ = rows;
    _apply_default_colors();
    dirty_ = true;
}

bool VTScreen::read_cell(int x, int y, VTRenderCell &out) const {
    if (x < 0 || y < 0 || x >= cols_ || y >= rows_) return false;

    VTermScreenCell c;
    VTermPos pos = {y, x};
    if (!vterm_screen_get_cell(screen_, pos, &c)) return false;

    convert_cell(c, screen_, default_fg_, default_bg_, out);
    return true;
}

void VTScreen::set_default_colors(const Color &fg, const Color &bg) {
    default_fg_ = fg;
    default_bg_ = bg;
    if (screen_ != nullptr) {
        _apply_default_colors();
    }
}

void VTScreen::_apply_default_colors() {
    VTermColor fg, bg;
    vterm_color_rgb(&fg,
                    static_cast<uint8_t>(default_fg_.r * 255),
                    static_cast<uint8_t>(default_fg_.g * 255),
                    static_cast<uint8_t>(default_fg_.b * 255));
    vterm_color_rgb(&bg,
                    static_cast<uint8_t>(default_bg_.r * 255),
                    static_cast<uint8_t>(default_bg_.g * 255),
                    static_cast<uint8_t>(default_bg_.b * 255));
    fg.type |= VTERM_COLOR_DEFAULT_FG;
    bg.type |= VTERM_COLOR_DEFAULT_BG;
    vterm_screen_set_default_colors(screen_, &fg, &bg);
}

void VTScreen::keyboard_unichar(uint32_t codepoint, int mod) {
    if (vt_ == nullptr) return;
    vterm_keyboard_unichar(vt_, codepoint, static_cast<VTermModifier>(mod));
}

void VTScreen::keyboard_key(int vt_key, int mod) {
    if (vt_ == nullptr) return;
    VTermKey key = to_vterm_key(vt_key);
    if (key == VTERM_KEY_NONE) return;
    vterm_keyboard_key(vt_, key, static_cast<VTermModifier>(mod));
}

void VTScreen::take_output(std::vector<char> &out) {
    if (pending_output_.empty()) {
        out.clear();
        return;
    }
    out.swap(pending_output_);
    pending_output_.clear();
}

void VTScreen::_on_output(const char *bytes, std::size_t len) {
    if (bytes == nullptr || len == 0) return;
    pending_output_.insert(pending_output_.end(), bytes, bytes + len);
}

void VTScreen::set_max_scrollback(int n) {
    if (n < 0) n = 0;
    max_scrollback_ = n;
    while (static_cast<int>(scrollback_.size()) > max_scrollback_) {
        scrollback_.pop_front();
    }
}

bool VTScreen::read_scrollback_cell(int x, int line_index, VTRenderCell &out) const {
    if (line_index < 0 || line_index >= static_cast<int>(scrollback_.size())) {
        return false;
    }
    const std::vector<VTRenderCell> &line = scrollback_[line_index];
    if (x < 0 || x >= static_cast<int>(line.size())) {
        // Past end of saved line: treat as empty cell with default colors.
        out = VTRenderCell{};
        out.fg = default_fg_;
        out.bg = Color(0, 0, 0, 0);
        return true;
    }
    out = line[x];
    return true;
}

void VTScreen::clear_scrollback() {
    scrollback_.clear();
}

void VTScreen::_save_scrollback_line(std::vector<VTRenderCell> &&line) {
    scrollback_.push_back(std::move(line));
    while (static_cast<int>(scrollback_.size()) > max_scrollback_) {
        scrollback_.pop_front();
    }
    dirty_ = true;
}

void VTScreen::_pushline_from_libvterm(int cols, const void *cells_blob) {
    if (cells_blob == nullptr || cols <= 0 || max_scrollback_ <= 0) return;
    const VTermScreenCell *cells = static_cast<const VTermScreenCell *>(cells_blob);
    std::vector<VTRenderCell> line;
    line.reserve(cols);
    for (int i = 0; i < cols; i++) {
        VTRenderCell rc;
        convert_cell(cells[i], screen_, default_fg_, default_bg_, rc);
        line.push_back(std::move(rc));
    }
    _save_scrollback_line(std::move(line));
}

void VTScreen::_on_damage() {
    dirty_ = true;
}
void VTScreen::_on_movecursor(int x, int y, bool visible) {
    cursor_ = Vector2i(x, y);
    cursor_visible_ = visible;
    dirty_ = true;
}
void VTScreen::_on_resize(int cols, int rows) {
    cols_ = cols;
    rows_ = rows;
    dirty_ = true;
}

} // namespace godot
