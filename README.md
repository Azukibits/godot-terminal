# godot-terminal

**English** · [中文](README.zh-CN.md)

> Embed a real terminal inside Godot's editor — run `cmd.exe`, `claude-code`,
> `codex`, or any TUI program without leaving the engine.

A C++ GDExtension for Godot 4.3+ that adds a **Terminal** tab to the editor's
bottom panel. Built on libvterm with native PTY backends per platform
(ConPTY on Windows, `forkpty` on macOS/Linux), so any modern command-line
tool (vim-style apps, AI coding assistants, build watchers, REPLs) works the
same as it would in your usual terminal emulator.

Please always use the latest version. More features will be added.

The `Terminal` class is also a regular `Control` node, so you can drop one
into a runtime scene if you want an in-game console.

[![build](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml)

![Godot editor with the Terminal tab open in the bottom panel](docs/img1.png)

Switch shells and TUI tools in the same panel — no separate window, no
context switch:

![Same panel running OpenAI Codex right after a claude-code session](docs/img2.png)

## Features

- Real ANSI/xterm terminal emulator powered by **libvterm 0.3.3**
  (truecolor, 256-color, indexed palette, alt-screen, mouse-aware programs)
- **Cross-platform PTY**: Windows ConPTY, macOS / Linux `forkpty`
- Spawns child processes (`cmd.exe`, `powershell.exe`, `bash`, `zsh`,
  `claude-code`, `codex`, …)
- **Auto-resize**: cols/rows recompute from the panel's current pixel
  size, and the resize is forwarded to the child process so TUI apps
  reflow correctly
- Full keyboard support: arrows, `F1`–`F12`, `Ctrl/Alt` combos, `Tab`,
  `Esc`, `Backspace`, function keys
- **Glyph styles**: bold (synthetic), underline, strikethrough rendered
  per-cell from libvterm SGR attrs
- **Blinking cursor** with shapes driven by `DECSCUSR` (block / underline /
  bar); hollow when the terminal isn't focused
- **Mouse selection** with click-and-drag; **Ctrl+Shift+C** copies,
  **Ctrl+Shift+V** pastes (bracketed-paste aware); middle-click also pastes
- 5000-line **scrollback** with mouse-wheel navigation; key input
  auto-snaps to the live view
- Mouse wheel scrolling; **Shift+wheel** scrolls a page
- Shells start in your **open Godot project's directory** by default,
  so AI coding tools see the right codebase

## Requirements

- **Godot 4.3+**
- One of:
  - **Windows 10 1809+** (ConPTY API)
  - **macOS 11+** (universal binary, x86_64 + arm64)
  - **Linux** (x86_64, glibc — uses `forkpty` from `libutil`)

## Quick install (recommended)

1. From the [Releases page](https://github.com/Azukibits/godot-terminal/releases),
   grab the zip for your platform:
   - Windows: `godot_terminal-vX.Y.Z-win64.zip`
   - macOS:   `godot_terminal-vX.Y.Z-macos-universal.zip`
   - Linux:   `godot_terminal-vX.Y.Z-linux-x86_64.zip`
2. Extract it. Copy the `godot_terminal/` folder it contains into your
   Godot project's `addons/` directory, so you end up with
   `your_project/addons/godot_terminal/`.
3. In Godot, open *Project → Project Settings → Plugins* and tick
   **godot_terminal** to enable it.
4. A **Terminal** tab now appears in the editor's bottom panel
   (next to *Output*, *Debugger*, *Audio*). Click it, click inside the
   panel to focus, type away.

## Troubleshooting

### macOS blocks the GDExtension dylib after downloading

If you installed the plugin from a downloaded `.zip`, macOS Gatekeeper may
mark the included `.dylib` files as quarantined.

When this happens, Godot may fail to load the GDExtension library even though
the files are in the correct location.

The plugin folder should look like this inside your Godot project:

```text
your_project/
└── addons/
    └── godot_terminal/
        └── bin/
            ├── libgodot_terminal.macos.template_debug.framework/
            └── libgodot_terminal.macos.template_release.framework/
```

To remove the quarantine attribute from the whole plugin folder, close Godot
and run the following command. Replace the path with your real project path:

```sh
xattr -dr com.apple.quarantine ~/your_project/addons/godot_terminal
```

Then reopen Godot and enable the plugin again:

```text
Project → Project Settings → Plugins → godot_terminal
```

If you still have the original downloaded `.zip`, you can also remove the
quarantine attribute from the zip before extracting it:

```sh
xattr -d com.apple.quarantine ~/Downloads/godot_terminal-vX.Y.Z-macos-universal.zip
```

Then extract it again into your project's `addons/` folder.

> Only remove the quarantine attribute from files downloaded from a trusted
> source, such as this repository's official Releases page, or files you built
> yourself from source.

### Windows blocks the GDExtension DLL after downloading

If you installed the plugin from a downloaded `.zip`, Windows may mark the
included `.dll` files as downloaded from the Internet. This is called
`Zone.Identifier` / Mark-of-the-Web.

When this happens, Godot may fail to load the GDExtension library even though
the files are in the correct location.

The plugin folder should look like this inside your Godot project:

```text
your_project/
└── addons/
    └── godot_terminal/
        └── bin/
            ├── godot_terminal.windows.template_debug.x86_64.dll
            └── godot_terminal.windows.template_release.x86_64.dll
```

To check whether the DLLs are blocked, close Godot and run the following command
in PowerShell. Replace the path with your real project path:

```powershell
Get-Item "C:\path\to\your_project\addons\godot_terminal\bin\*.dll" | ForEach-Object {
    $z = Get-Item $_.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue
    "{0}  --  Zone: {1}" -f $_.Name, $(if ($z) { 'BLOCKED' } else { 'ok' })
}
```

Example output when the DLLs are blocked:

```text
godot_terminal.windows.template_debug.x86_64.dll  --  Zone: BLOCKED
godot_terminal.windows.template_release.x86_64.dll  --  Zone: BLOCKED
```

To unblock only the DLL files, run:

```powershell
Unblock-File -Path "C:\path\to\your_project\addons\godot_terminal\bin\*.dll"
```

To unblock the entire plugin folder, run:

```powershell
Get-ChildItem "C:\path\to\your_project\addons\godot_terminal" -Recurse | Unblock-File
```

After unblocking, verify again:

```powershell
Get-Item "C:\path\to\your_project\addons\godot_terminal\bin\*.dll" | ForEach-Object {
    $z = Get-Item $_.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue
    "{0}  --  Zone: {1}" -f $_.Name, $(if ($z) { 'BLOCKED' } else { 'ok' })
}
```

Expected output:

```text
godot_terminal.windows.template_debug.x86_64.dll  --  Zone: ok
godot_terminal.windows.template_release.x86_64.dll  --  Zone: ok
```

Then reopen Godot and enable the plugin again:

```text
Project → Project Settings → Plugins → godot_terminal
```

If you still have the original downloaded `.zip`, you can also unblock the zip
before extracting it:

```powershell
Unblock-File -Path "C:\path\to\godot_terminal-vX.Y.Z-win64.zip"
```

Then extract it again into your project's `addons/` folder.

> Only unblock files downloaded from a trusted source, such as this repository's
> official Releases page, or files you built yourself from source.

## Build from source

For developers who want to hack on the plugin or build for an
unreleased Godot version.

Prerequisites (all platforms):

- Python 3.8+
- SCons 4.x (`pip install scons`)
- A C++17 toolchain:
  - **Windows**: Visual Studio 2019/2022 with *Desktop development with C++*
  - **macOS**: Xcode command-line tools (`xcode-select --install`)
  - **Linux**: `gcc` / `clang` and the system's `libutil` headers (usually
    bundled with glibc)

```sh
git clone --recurse-submodules https://github.com/Azukibits/godot-terminal.git
cd godot-terminal

# Windows
scons platform=windows target=template_release arch=x86_64

# macOS (universal: x86_64 + arm64)
scons platform=macos target=template_release arch=universal

# Linux
scons platform=linux target=template_release arch=x86_64
```

The library is written into `demo/addons/godot_terminal/bin/`. Open
`demo/` in Godot 4.3+ to test against the bundled demo project. On
Windows SCons normally locates MSVC automatically via `vswhere`; if it
can't, run from a *Developer Command Prompt for VS* (or after
`vcvarsall.bat amd64`).

## Use from GDScript

The editor plugin auto-mounts a `Terminal` instance in the bottom panel.
You can also create one yourself at runtime:

```gdscript
var term := Terminal.new()
term.font_size = 14
term.size_flags_horizontal = Control.SIZE_EXPAND_FILL
term.size_flags_vertical = Control.SIZE_EXPAND_FILL
add_child(term)

# Spawn a shell. Empty cwd = inherit; otherwise an absolute path.
# Pick the binary appropriate for the host OS.
match OS.get_name():
    "Windows": term.start_process("powershell.exe", [], "C:/path/to/your/project")
    "macOS":   term.start_process("/bin/zsh", ["-l"], "/path/to/your/project")
    _:         term.start_process("/bin/bash", ["-l"], "/path/to/your/project")

# Pipe text directly to the child's stdin.
term.send_input("ls -la\n")

# Connect to lifecycle signals.
term.process_exited.connect(func(code): print("exited: ", code))
```

Selected API (see [`src/terminal.h`](src/terminal.h) for the full set):

| Member | Purpose |
|--------|---------|
| `start_process(exe, args, cwd)` | Spawn a child attached to a new pty |
| `stop_process()` | Kill the child and detach the PTY |
| `send_input(text)` / `send_input_bytes(data)` | Write to child stdin |
| `write_text(s)` / `write_bytes(b)` | Inject bytes directly into the VT parser (no PTY) |
| `cols` / `rows` | Cell-grid size; auto-recomputed from the Control's size |
| `font` / `font_size` | Use a `SystemFont` or `FontFile`; monospace recommended |
| `scroll_to_bottom()` / `scroll_by(n)` / `clear_scrollback()` | Scrollback view controls |
| `set_max_scrollback(n)` | Default 5000 lines |
| Signals: `process_started`, `process_exited(exit_code)` | Child lifecycle |

## Status / roadmap

What works today (`v0.3.0`):

- [x] GDExtension scaffolding, Godot 4.3+ load
- [x] libvterm-driven cell rendering with full color/style data plumbed
- [x] Cross-platform PTY: Windows ConPTY, macOS / Linux `forkpty`
- [x] Auto-resize cols/rows from the Control's pixel size; forwarded to child
- [x] Bold (synthetic), underline, strikethrough glyph rendering
- [x] Keyboard input mapping (arrows, F-keys, Ctrl/Alt, etc.)
- [x] Cursor blink + shape (block / underline / bar) via `DECSCUSR`
- [x] Mouse selection + Ctrl+Shift+C/V copy/paste (bracketed-paste aware)
- [x] Scrollback (5000 lines, mouse-wheel)
- [x] Shell `cwd` defaults to the open Godot project root

Planned next:

- [ ] Mouse-button forwarding to TUI apps (xterm mouse modes)
- [ ] Italic glyph rendering (needs an italic FontFile or canvas shear)
- [ ] OSC 0 / 2 window-title plumbed to a `title_changed` signal
- [ ] `claude-code` / `codex` compatibility pass — fix bugs surfaced
  by their richer TUI rendering

## License

MIT — see [LICENSE](LICENSE).

Bundled third-party code:

- **[godot-cpp](https://github.com/godotengine/godot-cpp)** — MIT
  (Godot Engine project)
- **[libvterm 0.3.3](https://www.leonerd.org.uk/code/libvterm/)** by
  Paul "LeoNerd" Evans — MIT (vendored under `thirdparty/libvterm/`)
