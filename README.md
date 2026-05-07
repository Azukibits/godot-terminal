# godot_terminal

A Godot 4.3+ GDExtension that embeds a real terminal (Windows ConPTY + libvterm)
into Godot, so you can run vibecoding tools like
[claude-code](https://github.com/anthropics/claude-code) and
[codex](https://github.com/openai/codex) inside the engine.

> **Status: Phase 1 (skeleton).** Builds and loads as a GDExtension; the
> `Terminal` node can be added to a scene. PTY and VT integration land in
> later phases — see [the roadmap](#roadmap).

## Platform support

- Windows 10 1809+ (ConPTY)

macOS / Linux are out of scope for the initial release. PRs welcome later.

## Build

Prerequisites:

- Visual Studio 2019/2022 with the *Desktop development with C++* workload
- Python 3.8+
- SCons 4.x (`pip install scons`)
- Godot 4.3+ to test the demo

Build the extension (run from a *Developer Command Prompt for VS* so `cl.exe`
is on PATH, or after `vcvarsall.bat amd64`):

```
git clone --recurse-submodules <your-fork-url>
cd godot_terminal
scons platform=windows target=template_debug arch=x86_64
```

The DLL is written to `demo/addons/godot_terminal/bin/`. Open `demo/` in
Godot 4.3+ — the extension is wired up as an editor plugin that adds a
**Terminal** tab to the editor's bottom panel (next to Output / Debugger /
Audio). The `Terminal` class is also a regular Control node, so you can
drop one into a runtime scene if you want an in-game terminal.

## Roadmap

| Phase | Status | Goal |
|-------|--------|------|
| 1 | done   | Skeleton: extension loads, `Terminal` node exists |
| 2 | done   | Font + dummy cell rendering (16-color palette demo) |
| 3 | done   | libvterm 0.3.3 vendored; cells rendered from real VT parser |
| 4 | done   | ConPTY: spawn child via `start_process()`, drain stdout, `send_input()` |
| 5 | done   | Keyboard input: arrows, F-keys, Ctrl/Alt combos via `vterm_keyboard_*` |
| 5.5 | done | Scrollback: mouse wheel scrolls up to 5000 lines; key input snaps to bottom |
| 6 | todo   | Cursor blink / mouse-button forwarding / selection / auto-resize / styled glyphs |
| 7 | todo   | claude-code / codex compatibility passes |
| 8 | todo   | Release on GitHub with CI builds |

## License

MIT. See [LICENSE](LICENSE).

Bundled third-party code:

- **godot-cpp** — MIT (Godot Engine contributors)
- **libvterm 0.3.3** — MIT (Paul "LeoNerd" Evans), vendored under
  `thirdparty/libvterm/`; see `thirdparty/libvterm/LICENSE`
