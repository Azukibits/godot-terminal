#pragma once

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// Forward-declarations only — no libvterm headers leak into our public API.
struct VTerm;
struct VTermScreen;

namespace godot {

// Per-cell rendering data, decoupled from libvterm's internal types so the
// Godot renderer can draw without including vterm.h.
struct VTRenderCell {
    char32_t ch = U' ';
    Color fg;
    Color bg;        // alpha 0 means "use Terminal.background_color"
    bool bold = false;
    bool italic = false;
    bool underline = false; // any underline style
    bool reverse_video = false;
    int width = 1;          // 1 normal, 2 wide. (width-0 right-half is skipped.)
};

// Mirror of libvterm's VTermModifier so callers don't need to include vterm.h.
// Bitfield: combine with operator|.
enum VTMod : int {
    VT_MOD_NONE  = 0x00,
    VT_MOD_SHIFT = 0x01,
    VT_MOD_ALT   = 0x02,
    VT_MOD_CTRL  = 0x04,
};

// Mirror of libvterm's VTermKey for the keys we map. Function keys are
// VT_KEY_F1 + (n - 1) for n=1..12.
enum VTKey : int {
    VT_KEY_NONE = 0,
    VT_KEY_ENTER,
    VT_KEY_TAB,
    VT_KEY_BACKSPACE,
    VT_KEY_ESCAPE,
    VT_KEY_UP,
    VT_KEY_DOWN,
    VT_KEY_LEFT,
    VT_KEY_RIGHT,
    VT_KEY_INS,
    VT_KEY_DEL,
    VT_KEY_HOME,
    VT_KEY_END,
    VT_KEY_PAGEUP,
    VT_KEY_PAGEDOWN,
    VT_KEY_KP_ENTER,
    VT_KEY_F1,
    // F2..F12 = VT_KEY_F1 + 1..11
};

// Thin C++ wrapper around libvterm's VTerm + VTermScreen layer.
class VTScreen {
public:
    VTScreen(int cols, int rows);
    ~VTScreen();

    VTScreen(const VTScreen &) = delete;
    VTScreen &operator=(const VTScreen &) = delete;

    // Push raw bytes (PTY output / hardcoded test data) into the parser.
    void feed(const char *bytes, std::size_t len);

    // Resize the terminal grid.
    void resize(int cols, int rows);

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    // Reads cell at (x, y). Returns false on out-of-bounds.
    // For width-0 cells (right half of a wide char) returns true with
    // out.width=0 so the caller can skip drawing them.
    bool read_cell(int x, int y, VTRenderCell &out) const;

    Vector2i cursor() const { return cursor_; }
    bool cursor_visible() const { return cursor_visible_; }

    // Used when a cell has DEFAULT_FG/DEFAULT_BG flags.
    void set_default_colors(const Color &fg, const Color &bg);

    bool dirty() const { return dirty_; }
    void consume_dirty() { dirty_ = false; }

    // Keyboard input. Calls libvterm to generate the appropriate escape
    // sequence; the bytes land in our internal output buffer (drained by
    // take_output()) and should be sent to the child process's stdin.
    void keyboard_unichar(uint32_t codepoint, int mod);
    void keyboard_key(int vt_key, int mod);

    // Drain any bytes libvterm produced (response to keyboard input or to
    // OSC queries from the child). Swaps into `out` and clears the buffer.
    void take_output(std::vector<char> &out);

    // --- Scrollback ---------------------------------------------------------
    // Lines that have scrolled off the top are saved here so the user can
    // scroll up to see them. Index 0 is the OLDEST saved line,
    // scrollback_lines()-1 is the most recent (the one just above the live
    // screen).
    int scrollback_lines() const { return static_cast<int>(scrollback_.size()); }
    int max_scrollback() const { return max_scrollback_; }
    void set_max_scrollback(int n);
    bool read_scrollback_cell(int x, int line_index, VTRenderCell &out) const;
    void clear_scrollback();

    // Internal — called by the C trampolines in vt_screen.cpp.
    void _on_damage();
    void _on_movecursor(int x, int y, bool visible);
    void _on_resize(int cols, int rows);
    void _on_output(const char *bytes, std::size_t len);
    void _save_scrollback_line(std::vector<VTRenderCell> &&line);
    // Called by sb_pushline trampoline. cells_blob is actually a
    // const VTermScreenCell* — kept opaque so vterm.h doesn't leak here.
    void _pushline_from_libvterm(int cols, const void *cells_blob);

private:
    VTerm *vt_ = nullptr;
    VTermScreen *screen_ = nullptr;
    int cols_ = 0;
    int rows_ = 0;

    Vector2i cursor_{0, 0};
    bool cursor_visible_ = true;
    bool dirty_ = true;

    Color default_fg_ = Color(0.88f, 0.88f, 0.88f);
    Color default_bg_ = Color(0.06f, 0.06f, 0.08f);

    std::vector<char> pending_output_;

    std::deque<std::vector<VTRenderCell>> scrollback_;
    int max_scrollback_ = 5000;

    void _apply_default_colors();
};

} // namespace godot
