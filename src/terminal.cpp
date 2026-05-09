#include "terminal.h"
#include "pty.h"
#include "vt_screen.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
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

inline std::string to_utf8(const String &s) {
    CharString u = s.utf8();
    return std::string(u.get_data(), static_cast<size_t>(u.length()));
}

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
            _auto_resize_grid();
            _redraw_soon();
        } break;
        case NOTIFICATION_THEME_CHANGED: {
            _measure_cell();
            _auto_resize_grid();
            _redraw_soon();
        } break;
        case NOTIFICATION_RESIZED: {
            _auto_resize_grid();
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
    _auto_resize_grid();
    _redraw_soon();
}
Ref<Font> Terminal::get_font() const { return font_; }

void Terminal::set_font_size(int p_size) {
    p_size = std::max(p_size, 1);
    if (font_size_ == p_size) return;
    font_size_ = p_size;
    _measure_cell();
    _auto_resize_grid();
    _redraw_soon();
}
int Terminal::get_font_size() const { return font_size_; }

void Terminal::set_cols(int p_cols) {
    p_cols = std::max(p_cols, 1);
    if (cols_ == p_cols) return;
    cols_ = p_cols;
    if (vt_) vt_->resize(cols_, rows_);
    if (pty_) pty_->resize(cols_, rows_);
    _redraw_soon();
}
int Terminal::get_cols() const { return cols_; }

void Terminal::set_rows(int p_rows) {
    p_rows = std::max(p_rows, 1);
    if (rows_ == p_rows) return;
    rows_ = p_rows;
    if (vt_) vt_->resize(cols_, rows_);
    if (pty_) pty_->resize(cols_, rows_);
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
    if (!vt_) _ensure_vt();
    if (pty_ && pty_->is_running()) {
        UtilityFunctions::push_warning("godot_terminal: a process is already running; call stop_process() first.");
        return false;
    }
    pty_ = make_pty();
    std::vector<std::string> args;
    args.reserve(p_args.size());
    for (int i = 0; i < p_args.size(); i++) {
        args.push_back(to_utf8(p_args[i]));
    }
    if (!pty_->start(to_utf8(p_executable), args, cols_, rows_, to_utf8(p_cwd))) {
        UtilityFunctions::push_error("godot_terminal: failed to start: ", p_executable);
        pty_.reset();
        return false;
    }
    emit_signal("process_started");
    return true;
}

void Terminal::stop_process() {
    if (pty_) {
        pty_->stop();
        pty_.reset();
    }
}

bool Terminal::is_process_running() const {
    return pty_ != nullptr && pty_->is_running();
}

void Terminal::send_input(const String &p_text) {
    if (!pty_) return;
    CharString utf8 = p_text.utf8();
    pty_->write(utf8.get_data(), static_cast<size_t>(utf8.length()));
}

void Terminal::send_input_bytes(const PackedByteArray &p_data) {
    if (!pty_ || p_data.size() == 0) return;
    pty_->write(reinterpret_cast<const char *>(p_data.ptr()),
                static_cast<size_t>(p_data.size()));
}

// --- input ------------------------------------------------------------------

void Terminal::_gui_input(const Ref<InputEvent> &p_event) {
    // Mouse wheel & click first (no early-return to InputEventKey path).
    Ref<InputEventMouseButton> mb = p_event;
    if (mb.is_valid()) {
        int btn = static_cast<int>(mb->get_button_index());
        if (mb->is_pressed()) {
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
                Vector2i ca = _local_to_cell_abs(mb->get_position());
                sel_anchor_col_ = ca.x;
                sel_anchor_row_ = ca.y;
                sel_focus_col_ = ca.x;
                sel_focus_row_ = ca.y;
                sel_dragging_ = true;
                _redraw_soon();
                accept_event();
                return;
            }
            if (btn == MOUSE_BUTTON_MIDDLE) {
                // Middle-click pastes (X11 convention; harmless on Windows).
                _paste_from_clipboard();
                accept_event();
                return;
            }
        } else {
            if (btn == MOUSE_BUTTON_LEFT && sel_dragging_) {
                sel_dragging_ = false;
                // A bare click (no drag) clears any selection.
                if (sel_anchor_row_ == sel_focus_row_ &&
                    sel_anchor_col_ == sel_focus_col_) {
                    _clear_selection();
                }
                _redraw_soon();
                accept_event();
                return;
            }
        }
    }

    Ref<InputEventMouseMotion> mm = p_event;
    if (mm.is_valid() && sel_dragging_) {
        Vector2i ca = _local_to_cell_abs(mm->get_position());
        if (ca.x != sel_focus_col_ || ca.y != sel_focus_row_) {
            sel_focus_col_ = ca.x;
            sel_focus_row_ = ca.y;
            _redraw_soon();
        }
        accept_event();
        return;
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

    // Ctrl+Shift+C / Ctrl+Shift+V intercepted before reaching the VT, so
    // Ctrl+C still sends SIGINT to the child unmodified.
    if (key->is_ctrl_pressed() && key->is_shift_pressed()) {
        if (kc == KEY_C) {
            _copy_selection_to_clipboard();
            accept_event();
            return;
        }
        if (kc == KEY_V) {
            _paste_from_clipboard();
            accept_event();
            return;
        }
    }

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
        // Reset blink so the cursor is visible immediately after input.
        blink_phase_on_ = true;
        blink_last_toggle_ms_ = Time::get_singleton()->get_ticks_msec();
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
    if (pty_) {
        pty_->write(out.data(), out.size());
    }
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

void Terminal::_auto_resize_grid() {
    if (cell_size_.x <= 0 || cell_size_.y <= 0) return;
    Vector2 size = get_size();
    if (size.x <= 0 || size.y <= 0) return;

    int new_cols = std::max(1, static_cast<int>(size.x / cell_size_.x));
    int new_rows = std::max(1, static_cast<int>(size.y / cell_size_.y));
    if (new_cols == cols_ && new_rows == rows_) return;

    cols_ = new_cols;
    rows_ = new_rows;
    if (vt_) vt_->resize(cols_, rows_);
    if (pty_) pty_->resize(cols_, rows_);
}

void Terminal::_on_process() {
    // Cursor blink: tick the phase every 530ms, repaint only on flips so
    // we're not requeuing a redraw every frame.
    if (vt_ && vt_->cursor_visible() && vt_->cursor_blink()) {
        uint64_t now = Time::get_singleton()->get_ticks_msec();
        if (blink_last_toggle_ms_ == 0) blink_last_toggle_ms_ = now;
        if (now - blink_last_toggle_ms_ >= 530) {
            blink_phase_on_ = !blink_phase_on_;
            blink_last_toggle_ms_ = now;
            _redraw_soon();
        }
    } else if (!blink_phase_on_) {
        // Blink disabled but we were in the off phase — force on and repaint.
        blink_phase_on_ = true;
        _redraw_soon();
    }

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

    // 2) Glyphs + per-cell line decorations.
    real_t underline_thick = std::max<real_t>(1.0f, cell_size_.y * 0.07f);
    real_t strike_thick    = std::max<real_t>(1.0f, cell_size_.y * 0.06f);
    for (int y = 0; y < rows; y++) {
        real_t baseline_y = y * cell_size_.y + ascent;
        for (int x = 0; x < cols; x++) {
            VTRenderCell c;
            if (!read_at(x, y, c)) continue;
            if (c.width == 0) continue;

            real_t cell_w = cell_size_.x * (c.width > 0 ? c.width : 1);
            real_t cell_x = x * cell_size_.x;

            if (c.ch != U' ' && c.ch != 0) {
                Vector2 baseline(cell_x, baseline_y);
                f->draw_char(get_canvas_item(), baseline, static_cast<int32_t>(c.ch),
                             font_size_, c.fg);
                // Fake-bold: redraw at +1px x-offset for stroke weight. Cheaper
                // than carrying a parallel bold FontFile resource.
                if (c.bold) {
                    f->draw_char(get_canvas_item(), baseline + Vector2(1, 0),
                                 static_cast<int32_t>(c.ch), font_size_, c.fg);
                }
            }

            if (c.underline) {
                Vector2 pos(cell_x, baseline_y + std::max<real_t>(1.0f, cell_size_.y * 0.08f));
                draw_rect(Rect2(pos, Vector2(cell_w, underline_thick)), c.fg);
            }
            if (c.strikethrough) {
                Vector2 pos(cell_x, y * cell_size_.y + cell_size_.y * 0.55f);
                draw_rect(Rect2(pos, Vector2(cell_w, strike_thick)), c.fg);
            }
        }
    }

    // 3) Selection overlay — translucent highlight over selected cells.
    if (_has_selection()) {
        int r0, c0, r1, c1;
        _normalize_selection(r0, c0, r1, c1);
        Color sel_color(0.30f, 0.55f, 1.00f, 0.35f);
        for (int y = 0; y < rows; y++) {
            int abs_row = (S - K) + y;
            if (abs_row < r0 || abs_row > r1) continue;
            int x_lo = (abs_row == r0) ? c0 : 0;
            int x_hi = (abs_row == r1) ? c1 : cols - 1;
            if (x_hi < x_lo) continue;
            Vector2 pos(x_lo * cell_size_.x, y * cell_size_.y);
            Vector2 ext((x_hi - x_lo + 1) * cell_size_.x, cell_size_.y);
            draw_rect(Rect2(pos, ext), sel_color);
        }
    }

    // 4) Cursor — shape from libvterm (block / underline / bar), with a
    // blink phase driven by _on_process. When scrolled into scrollback the
    // live cursor row maps to viewport y = live_cursor.y + K.
    bool draw_cursor = vt_->cursor_visible() &&
                       (!vt_->cursor_blink() || blink_phase_on_);
    if (draw_cursor) {
        Vector2i cur = vt_->cursor();
        int viewport_y = cur.y + K;
        if (cur.x >= 0 && cur.x < cols && viewport_y >= 0 && viewport_y < rows) {
            Vector2 cell_pos(cur.x * cell_size_.x, viewport_y * cell_size_.y);
            Color cursor_color = foreground_color_;
            cursor_color.a = has_focus() ? 0.7f : 0.4f;
            int shape = vt_->cursor_shape();
            if (!has_focus()) {
                // Unfocused: hollow block outline regardless of shape.
                real_t t = std::max<real_t>(1.0f, cell_size_.y * 0.08f);
                Color outline = cursor_color;
                outline.a = 0.55f;
                Vector2 inner_pos = cell_pos + Vector2(t, t);
                Vector2 inner_ext = cell_size_ - Vector2(2 * t, 2 * t);
                draw_rect(Rect2(cell_pos, cell_size_), outline);
                if (inner_ext.x > 0 && inner_ext.y > 0) {
                    draw_rect(Rect2(inner_pos, inner_ext), background_color_);
                }
            } else if (shape == VT_CURSOR_UNDERLINE) {
                real_t h = std::max<real_t>(2.0f, cell_size_.y * 0.15f);
                draw_rect(Rect2(Vector2(cell_pos.x, cell_pos.y + cell_size_.y - h),
                                Vector2(cell_size_.x, h)), cursor_color);
            } else if (shape == VT_CURSOR_BAR) {
                real_t w = std::max<real_t>(2.0f, cell_size_.x * 0.15f);
                draw_rect(Rect2(cell_pos, Vector2(w, cell_size_.y)), cursor_color);
            } else { // VT_CURSOR_BLOCK
                draw_rect(Rect2(cell_pos, cell_size_), cursor_color);
            }
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

// --- selection / clipboard --------------------------------------------------

bool Terminal::_has_selection() const {
    if (sel_anchor_row_ < 0) return false;
    if (sel_anchor_row_ == sel_focus_row_ && sel_anchor_col_ == sel_focus_col_) {
        return false;
    }
    return true;
}

void Terminal::_clear_selection() {
    sel_anchor_row_ = sel_anchor_col_ = -1;
    sel_focus_row_ = sel_focus_col_ = -1;
    sel_dragging_ = false;
}

void Terminal::_normalize_selection(int &r0, int &c0, int &r1, int &c1) const {
    int ar = sel_anchor_row_, ac = sel_anchor_col_;
    int fr = sel_focus_row_,  fc = sel_focus_col_;
    if (ar < fr || (ar == fr && ac <= fc)) {
        r0 = ar; c0 = ac; r1 = fr; c1 = fc;
    } else {
        r0 = fr; c0 = fc; r1 = ar; c1 = ac;
    }
}

bool Terminal::_read_abs_cell(int x, int abs_row, VTRenderCell &out) const {
    if (!vt_) return false;
    int S = vt_->scrollback_lines();
    if (abs_row < 0) return false;
    if (abs_row < S) {
        return vt_->read_scrollback_cell(x, abs_row, out);
    }
    return vt_->read_cell(x, abs_row - S, out);
}

Vector2i Terminal::_local_to_cell_abs(const Vector2 &local) const {
    int col = static_cast<int>(local.x / std::max<real_t>(cell_size_.x, 1.0f));
    int y   = static_cast<int>(local.y / std::max<real_t>(cell_size_.y, 1.0f));
    if (col < 0) col = 0;
    if (col > cols_ - 1) col = cols_ - 1;
    if (y < 0) y = 0;
    if (y > rows_ - 1) y = rows_ - 1;
    int S = vt_ ? vt_->scrollback_lines() : 0;
    int abs_row = (S - scroll_offset_) + y;
    return Vector2i(col, abs_row);
}

String Terminal::_build_selection_text() const {
    if (!_has_selection() || !vt_) return String();
    int r0, c0, r1, c1;
    _normalize_selection(r0, c0, r1, c1);

    String out;
    for (int row = r0; row <= r1; row++) {
        int x_lo = (row == r0) ? c0 : 0;
        int x_hi = (row == r1) ? c1 : cols_ - 1;

        std::u32string line;
        line.reserve(static_cast<size_t>(x_hi - x_lo + 1));
        for (int x = x_lo; x <= x_hi; x++) {
            VTRenderCell c;
            if (!_read_abs_cell(x, row, c)) continue;
            if (c.width == 0) continue; // right half of wide char
            line.push_back(c.ch == 0 ? U' ' : c.ch);
        }
        // Trim trailing spaces (but keep them if the row is fully selected
        // up to its right edge AND nothing follows on subsequent rows? — keep
        // it simple: always trim).
        while (!line.empty() && (line.back() == U' ')) line.pop_back();

        if (row != r0) out += "\n";
        out += String(reinterpret_cast<const char32_t *>(line.c_str()));
    }
    return out;
}

void Terminal::_copy_selection_to_clipboard() {
    if (!_has_selection()) return;
    String text = _build_selection_text();
    if (text.is_empty()) return;
    DisplayServer *ds = DisplayServer::get_singleton();
    if (ds) ds->clipboard_set(text);
}

void Terminal::_paste_from_clipboard() {
    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds || !vt_) return;
    String text = ds->clipboard_get();
    if (text.is_empty()) return;

    vt_->keyboard_start_paste();
    // Iterate codepoints (Godot String stores UTF-32-ish via char32_t).
    int n = text.length();
    for (int i = 0; i < n; i++) {
        char32_t cp = text[i];
        // Normalize Windows clipboard CRLF / lone CR to '\r' which terminals
        // expect from Enter. Strip the LF half.
        if (cp == U'\r') {
            vt_->keyboard_unichar(static_cast<uint32_t>('\r'), VT_MOD_NONE);
            if (i + 1 < n && text[i + 1] == U'\n') i++; // skip LF after CR
        } else if (cp == U'\n') {
            vt_->keyboard_unichar(static_cast<uint32_t>('\r'), VT_MOD_NONE);
        } else {
            vt_->keyboard_unichar(static_cast<uint32_t>(cp), VT_MOD_NONE);
        }
    }
    vt_->keyboard_end_paste();
    _flush_vt_output_to_pty();
    _redraw_soon();
}

} // namespace godot
