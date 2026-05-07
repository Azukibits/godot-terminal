#include "terminal.h"
#include "vt_screen.h"

#ifdef _WIN32
#include "pty_windows.h"
#endif

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace godot {

namespace {

#ifdef _WIN32
// Build a single Win32 command-line string by quoting each token if it
// contains whitespace. The first token is the executable.
std::wstring build_command_line(const String &exe, const PackedStringArray &args) {
    auto to_w = [](const String &s) -> std::wstring {
        Char16String u = s.utf16();
        return std::wstring(reinterpret_cast<const wchar_t *>(u.get_data()),
                            static_cast<size_t>(u.length()));
    };
    auto quote = [](const std::wstring &s) -> std::wstring {
        if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
        std::wstring out;
        out.reserve(s.size() + 2);
        out.push_back(L'"');
        for (wchar_t c : s) {
            if (c == L'"') out.push_back(L'\\');
            out.push_back(c);
        }
        out.push_back(L'"');
        return out;
    };
    std::wstring cmd = quote(to_w(exe));
    for (int i = 0; i < args.size(); i++) {
        cmd.push_back(L' ');
        cmd.append(quote(to_w(args[i])));
    }
    return cmd;
}
#endif

// Map a Godot Key keycode to a VTKey value. Returns VT_KEY_NONE for keys
// not classified as "special"; the caller should fall back to unichar
// translation in that case.
int godot_key_to_vt_key(int keycode) {
    using namespace godot;
    if (keycode >= KEY_F1 && keycode <= KEY_F12) {
        return VT_KEY_F1 + (keycode - KEY_F1);
    }
    switch (keycode) {
        case KEY_ENTER:    return VT_KEY_ENTER;
        case KEY_KP_ENTER: return VT_KEY_KP_ENTER;
        case KEY_TAB:      return VT_KEY_TAB;
        case KEY_BACKSPACE:return VT_KEY_BACKSPACE;
        case KEY_ESCAPE:   return VT_KEY_ESCAPE;
        case KEY_UP:       return VT_KEY_UP;
        case KEY_DOWN:     return VT_KEY_DOWN;
        case KEY_LEFT:     return VT_KEY_LEFT;
        case KEY_RIGHT:    return VT_KEY_RIGHT;
        case KEY_INSERT:   return VT_KEY_INS;
        case KEY_DELETE:   return VT_KEY_DEL;
        case KEY_HOME:     return VT_KEY_HOME;
        case KEY_END:      return VT_KEY_END;
        case KEY_PAGEUP:   return VT_KEY_PAGEUP;
        case KEY_PAGEDOWN: return VT_KEY_PAGEDOWN;
        default:           return VT_KEY_NONE;
    }
}

} // namespace

Terminal::Terminal() = default;
Terminal::~Terminal() = default;

void Terminal::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_font", "font"), &Terminal::set_font);
    ClassDB::bind_method(D_METHOD("get_font"), &Terminal::get_font);
    ClassDB::bind_method(D_METHOD("set_font_size", "size"), &Terminal::set_font_size);
    ClassDB::bind_method(D_METHOD("get_font_size"), &Terminal::get_font_size);
    ClassDB::bind_method(D_METHOD("set_cols", "cols"), &Terminal::set_cols);
    ClassDB::bind_method(D_METHOD("get_cols"), &Terminal::get_cols);
    ClassDB::bind_method(D_METHOD("set_rows", "rows"), &Terminal::set_rows);
    ClassDB::bind_method(D_METHOD("get_rows"), &Terminal::get_rows);
    ClassDB::bind_method(D_METHOD("set_background_color", "color"), &Terminal::set_background_color);
    ClassDB::bind_method(D_METHOD("get_background_color"), &Terminal::get_background_color);
    ClassDB::bind_method(D_METHOD("set_foreground_color", "color"), &Terminal::set_foreground_color);
    ClassDB::bind_method(D_METHOD("get_foreground_color"), &Terminal::get_foreground_color);
    ClassDB::bind_method(D_METHOD("get_cell_size"), &Terminal::get_cell_size);

    ClassDB::bind_method(D_METHOD("write_bytes", "data"), &Terminal::write_bytes);
    ClassDB::bind_method(D_METHOD("write_text", "text"), &Terminal::write_text);

    ClassDB::bind_method(D_METHOD("start_process", "executable", "args", "cwd"),
                         &Terminal::start_process,
                         DEFVAL(PackedStringArray()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("stop_process"), &Terminal::stop_process);
    ClassDB::bind_method(D_METHOD("is_process_running"), &Terminal::is_process_running);
    ClassDB::bind_method(D_METHOD("send_input", "text"), &Terminal::send_input);
    ClassDB::bind_method(D_METHOD("send_input_bytes", "data"), &Terminal::send_input_bytes);

    ClassDB::bind_method(D_METHOD("get_scrollback_lines"), &Terminal::get_scrollback_lines);
    ClassDB::bind_method(D_METHOD("get_scroll_offset"), &Terminal::get_scroll_offset);
    ClassDB::bind_method(D_METHOD("set_scroll_offset", "offset"), &Terminal::set_scroll_offset);
    ClassDB::bind_method(D_METHOD("scroll_to_bottom"), &Terminal::scroll_to_bottom);
    ClassDB::bind_method(D_METHOD("scroll_to_top"), &Terminal::scroll_to_top);
    ClassDB::bind_method(D_METHOD("scroll_by", "delta_lines"), &Terminal::scroll_by);
    ClassDB::bind_method(D_METHOD("clear_scrollback"), &Terminal::clear_scrollback);
    ClassDB::bind_method(D_METHOD("set_max_scrollback", "max"), &Terminal::set_max_scrollback);
    ClassDB::bind_method(D_METHOD("get_max_scrollback"), &Terminal::get_max_scrollback);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "font", PROPERTY_HINT_RESOURCE_TYPE, "Font"),
                 "set_font", "get_font");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "6,72,1"),
                 "set_font_size", "get_font_size");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "cols", PROPERTY_HINT_RANGE, "1,500,1"),
                 "set_cols", "get_cols");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "rows", PROPERTY_HINT_RANGE, "1,200,1"),
                 "set_rows", "get_rows");
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background_color"),
                 "set_background_color", "get_background_color");
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "foreground_color"),
                 "set_foreground_color", "get_foreground_color");

    ADD_SIGNAL(MethodInfo("process_exited", PropertyInfo(Variant::INT, "exit_code")));
    ADD_SIGNAL(MethodInfo("process_started"));
}

void Terminal::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_READY: {
            _measure_cell();
            _ensure_vt();
            set_focus_mode(Control::FOCUS_ALL);
            set_process(true); // drain PTY every frame
            _redraw_soon();
        } break;
        case NOTIFICATION_THEME_CHANGED: {
            _measure_cell();
            _redraw_soon();
        } break;
        case NOTIFICATION_RESIZED: {
            _redraw_soon();
        } break;
        case NOTIFICATION_DRAW: {
            _on_draw();
        } break;
        case NOTIFICATION_PROCESS: {
            _on_process();
        } break;
        case NOTIFICATION_EXIT_TREE: {
            stop_process();
        } break;
        default:
            break;
    }
}

// --- properties ---------------------------------------------------------------

void Terminal::set_font(const Ref<Font> &p_font) {
    if (font_ == p_font) return;
    font_ = p_font;
    _measure_cell();
    _redraw_soon();
}
Ref<Font> Terminal::get_font() const { return font_; }

void Terminal::set_font_size(int p_size) {
    p_size = std::max(p_size, 1);
    if (font_size_ == p_size) return;
    font_size_ = p_size;
    _measure_cell();
    _redraw_soon();
}
int Terminal::get_font_size() const { return font_size_; }

void Terminal::set_cols(int p_cols) {
    p_cols = std::max(p_cols, 1);
    if (cols_ == p_cols) return;
    cols_ = p_cols;
    if (vt_) vt_->resize(cols_, rows_);
#ifdef _WIN32
    if (pty_) pty_->resize(cols_, rows_);
#endif
    _redraw_soon();
}
int Terminal::get_cols() const { return cols_; }

void Terminal::set_rows(int p_rows) {
    p_rows = std::max(p_rows, 1);
    if (rows_ == p_rows) return;
    rows_ = p_rows;
    if (vt_) vt_->resize(cols_, rows_);
#ifdef _WIN32
    if (pty_) pty_->resize(cols_, rows_);
#endif
    _redraw_soon();
}
int Terminal::get_rows() const { return rows_; }

void Terminal::set_background_color(const Color &p_color) {
    background_color_ = p_color;
    if (vt_) vt_->set_default_colors(foreground_color_, background_color_);
    _redraw_soon();
}
Color Terminal::get_background_color() const { return background_color_; }

void Terminal::set_foreground_color(const Color &p_color) {
    foreground_color_ = p_color;
    if (vt_) vt_->set_default_colors(foreground_color_, background_color_);
    _redraw_soon();
}
Color Terminal::get_foreground_color() const { return foreground_color_; }

Vector2 Terminal::get_cell_size() const { return cell_size_; }

// --- VT-side input -----------------------------------------------------------

void Terminal::write_bytes(const PackedByteArray &p_data) {
    if (!vt_) _ensure_vt();
    if (p_data.size() == 0) return;
    vt_->feed(reinterpret_cast<const char *>(p_data.ptr()),
              static_cast<size_t>(p_data.size()));
    _redraw_soon();
}

void Terminal::write_text(const String &p_text) {
    if (!vt_) _ensure_vt();
    CharString utf8 = p_text.utf8();
    vt_->feed(utf8.get_data(), static_cast<size_t>(utf8.length()));
    _redraw_soon();
}

// --- PTY-side ---------------------------------------------------------------

bool Terminal::start_process(const String &p_executable,
                              const PackedStringArray &p_args,
                              const String &p_cwd) {
#ifdef _WIN32
    if (!vt_) _ensure_vt();
    if (pty_ && pty_->is_running()) {
        UtilityFunctions::push_warning("godot_terminal: a process is already running; call stop_process() first.");
        return false;
    }
    pty_ = std::make_unique<PtyWindows>();
    std::wstring cmd = build_command_line(p_executable, p_args);
    std::wstring cwd_w;
    if (!p_cwd.is_empty()) {
        Char16String u = p_cwd.utf16();
        cwd_w.assign(reinterpret_cast<const wchar_t *>(u.get_data()),
                     static_cast<size_t>(u.length()));
    }
    if (!pty_->start(cmd, cols_, rows_, cwd_w)) {
        UtilityFunctions::push_error("godot_terminal: failed to start: ", p_executable);
        pty_.reset();
        return false;
    }
    emit_signal("process_started");
    return true;
#else
    (void)p_cwd;
    UtilityFunctions::push_error("godot_terminal: PTY only implemented on Windows so far.");
    return false;
#endif
}

void Terminal::stop_process() {
#ifdef _WIN32
    if (pty_) {
        pty_->stop();
        pty_.reset();
    }
#endif
}

bool Terminal::is_process_running() const {
#ifdef _WIN32
    return pty_ != nullptr && pty_->is_running();
#else
    return false;
#endif
}

void Terminal::send_input(const String &p_text) {
#ifdef _WIN32
    if (!pty_) return;
    CharString utf8 = p_text.utf8();
    pty_->write(utf8.get_data(), static_cast<size_t>(utf8.length()));
#endif
}

void Terminal::send_input_bytes(const PackedByteArray &p_data) {
#ifdef _WIN32
    if (!pty_ || p_data.size() == 0) return;
    pty_->write(reinterpret_cast<const char *>(p_data.ptr()),
                static_cast<size_t>(p_data.size()));
#endif
}

// --- input ------------------------------------------------------------------

void Terminal::_gui_input(const Ref<InputEvent> &p_event) {
    // Mouse wheel & click first (no early-return to InputEventKey path).
    Ref<InputEventMouseButton> mb = p_event;
    if (mb.is_valid() && mb->is_pressed()) {
        int btn = static_cast<int>(mb->get_button_index());
        if (btn == MOUSE_BUTTON_WHEEL_UP) {
            int amount = mb->is_shift_pressed() ? rows_ - 1 : 3;
            scroll_by(amount);
            accept_event();
            return;
        }
        if (btn == MOUSE_BUTTON_WHEEL_DOWN) {
            int amount = mb->is_shift_pressed() ? rows_ - 1 : 3;
            scroll_by(-amount);
            accept_event();
            return;
        }
        if (btn == MOUSE_BUTTON_LEFT) {
            grab_focus();
            // Don't accept_event so the panel can still receive clicks for selection later.
        }
    }

    Ref<InputEventKey> key = p_event;
    if (key.is_null()) return;
    if (!key->is_pressed()) return; // key-release events are not forwarded
    if (!vt_) return;

    int mod = VT_MOD_NONE;
    if (key->is_ctrl_pressed())  mod |= VT_MOD_CTRL;
    if (key->is_alt_pressed())   mod |= VT_MOD_ALT;
    if (key->is_shift_pressed()) mod |= VT_MOD_SHIFT;

    int kc = static_cast<int>(key->get_keycode());
    int vt_key = godot_key_to_vt_key(kc);

    bool handled = false;

    if (vt_key != VT_KEY_NONE) {
        vt_->keyboard_key(vt_key, mod);
        handled = true;
    } else {
        int32_t ch_signed = static_cast<int32_t>(key->get_unicode());
        if (ch_signed > 0 && ch_signed <= 0x10FFFF) {
            // For printable chars, shift is already baked into the case of `ch`.
            // Pass only Ctrl/Alt to libvterm so e.g. 'A' doesn't turn into Shift+'A'.
            int unichar_mod = mod & (VT_MOD_CTRL | VT_MOD_ALT);
            vt_->keyboard_unichar(static_cast<uint32_t>(ch_signed), unichar_mod);
            handled = true;
        } else if (mod & VT_MOD_CTRL) {
            // Ctrl+<letter> sometimes arrives with unicode=0 (e.g. Ctrl+@ → 0).
            // Fall back to keycode if it's an ASCII letter.
            if (kc >= KEY_A && kc <= KEY_Z) {
                vt_->keyboard_unichar(static_cast<uint32_t>('a' + (kc - KEY_A)),
                                      VT_MOD_CTRL);
                handled = true;
            }
        }
    }

    if (handled) {
        // Any actual key input snaps the viewport back to the live screen so
        // the user sees what they typed.
        if (scroll_offset_ != 0) {
            scroll_offset_ = 0;
            _redraw_soon();
        }
        _flush_vt_output_to_pty();
        accept_event();
    }
}

// --- scrollback ----------------------------------------------------------

int Terminal::get_scrollback_lines() const {
    return vt_ ? vt_->scrollback_lines() : 0;
}

void Terminal::set_scroll_offset(int p_offset) {
    int max_off = vt_ ? vt_->scrollback_lines() : 0;
    if (p_offset < 0) p_offset = 0;
    if (p_offset > max_off) p_offset = max_off;
    if (p_offset == scroll_offset_) return;
    scroll_offset_ = p_offset;
    _redraw_soon();
}

void Terminal::scroll_to_bottom() { set_scroll_offset(0); }

void Terminal::scroll_to_top() {
    set_scroll_offset(vt_ ? vt_->scrollback_lines() : 0);
}

void Terminal::scroll_by(int p_delta_lines) {
    set_scroll_offset(scroll_offset_ + p_delta_lines);
}

void Terminal::clear_scrollback() {
    if (vt_) vt_->clear_scrollback();
    scroll_offset_ = 0;
    _redraw_soon();
}

void Terminal::set_max_scrollback(int p_max) {
    if (vt_) vt_->set_max_scrollback(p_max);
}

int Terminal::get_max_scrollback() const {
    return vt_ ? vt_->max_scrollback() : 5000;
}

void Terminal::_flush_vt_output_to_pty() {
    if (!vt_) return;
    std::vector<char> out;
    vt_->take_output(out);
    if (out.empty()) return;
#ifdef _WIN32
    if (pty_) {
        pty_->write(out.data(), out.size());
    }
#endif
}

// --- internals ---------------------------------------------------------------

Ref<Font> Terminal::_resolve_font() const {
    if (font_.is_valid()) return font_;
    return get_theme_default_font();
}

void Terminal::_measure_cell() {
    Ref<Font> f = _resolve_font();
    if (f.is_null()) {
        cell_size_ = Vector2(8, 16);
        return;
    }
    real_t line_h = f->get_height(font_size_);
    Vector2 m_size = f->get_char_size(static_cast<int32_t>('M'), font_size_);
    real_t adv = m_size.x > 0 ? m_size.x : line_h * 0.6f;
    cell_size_ = Vector2(std::ceil(adv), std::ceil(line_h));
}

void Terminal::_ensure_vt() {
    if (!vt_) {
        vt_ = std::make_unique<VTScreen>(cols_, rows_);
    } else if (vt_->cols() != cols_ || vt_->rows() != rows_) {
        vt_->resize(cols_, rows_);
    }
    vt_->set_default_colors(foreground_color_, background_color_);
}

void Terminal::_redraw_soon() {
    queue_redraw();
}

void Terminal::_on_process() {
#ifdef _WIN32
    if (!pty_) return;

    bool got_data = false;
    pty_->drain([this, &got_data](const char *data, size_t len) {
        if (vt_ && len > 0) {
            vt_->feed(data, len);
            got_data = true;
        }
    });

    if (got_data || (vt_ && vt_->dirty())) {
        if (vt_) vt_->consume_dirty();
        _redraw_soon();
    }

    if (pty_->exited()) {
        int code = pty_->exit_code();
        // Ensure final bytes are flushed before announcing exit.
        pty_->drain([this](const char *data, size_t len) {
            if (vt_ && len > 0) vt_->feed(data, len);
        });
        pty_.reset();
        emit_signal("process_exited", code);
        _redraw_soon();
    }
#endif
}

void Terminal::_on_draw() {
    Vector2 size = get_size();

    draw_rect(Rect2(Vector2(), size), background_color_);

    Ref<Font> f = _resolve_font();
    if (f.is_null() || !vt_) return;

    real_t ascent = f->get_ascent(font_size_);
    const int cols = cols_;
    const int rows = rows_;
    const int S = vt_->scrollback_lines();
    int K = scroll_offset_;
    if (K < 0) K = 0;
    if (K > S) K = S;

    // Lambda: read the cell for viewport position (x, y), where y=0 is the
    // top of the visible region. Returns false for non-readable.
    // Logic: abs_row = (S - K) + y; if abs_row < S, read scrollback; else read live.
    auto read_at = [&](int x, int y, VTRenderCell &out) -> bool {
        int abs_row = (S - K) + y;
        if (abs_row < S) {
            return vt_->read_scrollback_cell(x, abs_row, out);
        }
        return vt_->read_cell(x, abs_row - S, out);
    };

    // 1) Backgrounds.
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            VTRenderCell c;
            if (!read_at(x, y, c)) continue;
            if (c.width == 0) continue;
            if (c.bg.a <= 0.0f) continue;
            Vector2 cell_pos(x * cell_size_.x, y * cell_size_.y);
            Vector2 cell_extent(cell_size_.x * (c.width > 0 ? c.width : 1), cell_size_.y);
            draw_rect(Rect2(cell_pos, cell_extent), c.bg);
        }
    }

    // 2) Glyphs.
    for (int y = 0; y < rows; y++) {
        real_t baseline_y = y * cell_size_.y + ascent;
        for (int x = 0; x < cols; x++) {
            VTRenderCell c;
            if (!read_at(x, y, c)) continue;
            if (c.width == 0) continue;
            if (c.ch == U' ' || c.ch == 0) continue;
            Vector2 baseline(x * cell_size_.x, baseline_y);
            f->draw_char(get_canvas_item(), baseline, static_cast<int32_t>(c.ch),
                         font_size_, c.fg);
        }
    }

    // 3) Cursor — only inside the visible viewport. When scrolled into
    // scrollback, the live cursor row maps to viewport y = live_cursor.y + K.
    if (vt_->cursor_visible()) {
        Vector2i cur = vt_->cursor();
        int viewport_y = cur.y + K;
        if (cur.x >= 0 && cur.x < cols && viewport_y >= 0 && viewport_y < rows) {
            Vector2 cell_pos(cur.x * cell_size_.x, viewport_y * cell_size_.y);
            Color cursor_color = foreground_color_;
            cursor_color.a = 0.6f;
            draw_rect(Rect2(cell_pos, cell_size_), cursor_color);
        }
    }

    // Right-edge stripe: visual hint that the viewport is in scrollback.
    if (K > 0) {
        Color stripe = foreground_color_;
        stripe.a = 0.25f;
        real_t bar_w = std::max<real_t>(2.0f, cell_size_.x * 0.15f);
        draw_rect(Rect2(Vector2(size.x - bar_w, 0), Vector2(bar_w, size.y)), stripe);
    }
}

} // namespace godot
