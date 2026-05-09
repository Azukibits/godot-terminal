#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <memory>

namespace godot {

class VTScreen;
class IPty;

class Terminal : public Control {
    GDCLASS(Terminal, Control)

public:
    Terminal();
    ~Terminal();

    void set_font(const Ref<Font> &p_font);
    Ref<Font> get_font() const;

    void set_font_size(int p_size);
    int get_font_size() const;

    void set_cols(int p_cols);
    int get_cols() const;

    void set_rows(int p_rows);
    int get_rows() const;

    void set_background_color(const Color &p_color);
    Color get_background_color() const;

    void set_foreground_color(const Color &p_color);
    Color get_foreground_color() const;

    Vector2 get_cell_size() const;

    // Scrollback view controls.
    int get_scrollback_lines() const;
    int get_scroll_offset() const { return scroll_offset_; }
    void set_scroll_offset(int p_offset);
    void scroll_to_bottom();
    void scroll_to_top();
    void scroll_by(int p_delta_lines);
    void clear_scrollback();
    void set_max_scrollback(int p_max);
    int get_max_scrollback() const;

    // VT-side: feed bytes directly into the parser (display only, useful for
    // testing escape sequences without a child process).
    void write_bytes(const PackedByteArray &p_data);
    void write_text(const String &p_text);

    // PTY-side: spawn a child process attached to a new ConPTY.
    // p_cwd is the working directory for the child. Empty string ("")
    // means "inherit parent process cwd" (Godot editor's working directory).
    bool start_process(const String &p_executable,
                       const PackedStringArray &p_args = PackedStringArray(),
                       const String &p_cwd = String());
    void stop_process();
    bool is_process_running() const;

    // Send bytes to the running child's stdin. Keyboard events on the
    // Terminal control already feed this path automatically; these methods
    // are exposed for scripted input (e.g. paste, programmatic commands).
    void send_input(const String &p_text);
    void send_input_bytes(const PackedByteArray &p_data);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void _gui_input(const Ref<InputEvent> &p_event) override;

private:
    Ref<Font> font_;
    int font_size_ = 14;
    int cols_ = 80;
    int rows_ = 24;
    Color background_color_ = Color(0.06f, 0.06f, 0.08f);
    Color foreground_color_ = Color(0.88f, 0.88f, 0.88f);

    Vector2 cell_size_ = Vector2(8, 16);

    // 0 = bottom (live view). >0 means viewport is shifted up by N lines into
    // scrollback. Clamped to [0, vt_->scrollback_lines()].
    int scroll_offset_ = 0;

    // Cursor blink: phase toggles every ~530ms while libvterm reports
    // blinking is on. Typing resets the phase to "on" so the cursor stays
    // visible during input.
    bool blink_phase_on_ = true;
    uint64_t blink_last_toggle_ms_ = 0;

    // Mouse selection in absolute-row coordinates (stable across scroll).
    // abs_row in [0, vt_->scrollback_lines()) = scrollback line index;
    // abs_row in [scrollback_lines(), scrollback_lines() + rows_) = live row.
    // Anchor row = -1 means no selection.
    int sel_anchor_row_ = -1;
    int sel_anchor_col_ = -1;
    int sel_focus_row_ = -1;
    int sel_focus_col_ = -1;
    bool sel_dragging_ = false;

    std::unique_ptr<VTScreen> vt_;
    std::unique_ptr<IPty> pty_;

    Ref<Font> _resolve_font() const;
    void _measure_cell();
    void _ensure_vt();
    void _redraw_soon();

    // Recompute cols/rows from the current Control size and cell_size_,
    // forwarding the new grid to vt_ and pty_. No-op if sizes are zero.
    void _auto_resize_grid();

    void _on_draw();
    void _on_process();

    // Selection helpers.
    bool _has_selection() const;
    void _clear_selection();
    void _normalize_selection(int &r0, int &c0, int &r1, int &c1) const;
    bool _read_abs_cell(int x, int abs_row, struct VTRenderCell &out) const;
    Vector2i _local_to_cell_abs(const Vector2 &local) const; // -> (col, abs_row)
    String _build_selection_text() const;
    void _copy_selection_to_clipboard();
    void _paste_from_clipboard();

    // Drains libvterm output (responses to keypresses/OSC) and writes to PTY.
    void _flush_vt_output_to_pty();
};

} // namespace godot
