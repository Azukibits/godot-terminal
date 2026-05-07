# godot-terminal

**English** · [中文](README.zh-CN.md)

> Embed a real terminal inside Godot's editor — run `cmd.exe`, `claude-code`,
> `codex`, or any TUI program without leaving the engine.

A C++ GDExtension for Godot 4.3+ that adds a **Terminal** tab to the editor's
bottom panel. Built on Windows ConPTY + libvterm, so any modern command-line
tool (vim-style apps, AI coding assistants, build watchers, REPLs) works the
same as it would in Windows Terminal.

please always use the latest version 
More features will be added

The `Terminal` class is also a regular `Control` node, so you can drop one
into a runtime scene if you want an in-game console.

[![build](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml)

![Godot editor with the Terminal tab open in the bottom panel](docs/img1.png)

Switch shells and TUI tools in the same panel — no separate window, no
context switch:

![Same panel running OpenAI Codex right after a claude-code session](docs/img2.png)

## Features

- Real ANSI/xterm terminal emulator powered by **libvterm 0.3.3**
  (truecolor, 256-color, indexed palette, bold/italic/underline,
  alt-screen, mouse-aware programs)
- Spawns child processes via Windows **ConPTY**
  (`cmd.exe`, `powershell.exe`, `claude-code`, `codex`, …)
- Full keyboard support: arrows, `F1`–`F12`, `Ctrl/Alt` combos, `Tab`,
  `Esc`, `Backspace`, function keys
- 5000-line **scrollback** with mouse-wheel navigation; key input
  auto-snaps to the live view
- Mouse wheel scrolling; **Shift+wheel** scrolls a page
- Shells start in your **open Godot project's directory** by default,
  so AI coding tools see the right codebase
- No runtime dependencies beyond a Godot 4.3+ editor and a Win10 1809+ host

## Requirements

- **Windows 10 1809+** (ConPTY API)
- **Godot 4.3+**

macOS / Linux are on the roadmap.

## Quick install (recommended)

1. Grab the latest **`godot_terminal-vX.Y.Z-win64.zip`** from the
   [Releases page](https://github.com/Azukibits/godot-terminal/releases).
2. Extract it. Copy the `godot_terminal/` folder it contains into your
   Godot project's `addons/` directory, so you end up with
   `your_project/addons/godot_terminal/`.
3. In Godot, open *Project → Project Settings → Plugins* and tick
   **godot_terminal** to enable it.
4. A **Terminal** tab now appears in the editor's bottom panel
   (next to *Output*, *Debugger*, *Audio*). Click it, click inside the
   panel to focus, type away.

## Troubleshooting

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
unreleased Godot/Windows combination.

Prerequisites:

- Visual Studio 2019/2022 with the *Desktop development with C++* workload
- Python 3.8+
- SCons 4.x (`pip install scons`)

```sh
git clone --recurse-submodules https://github.com/Azukibits/godot-terminal.git
cd godot-terminal
scons platform=windows target=template_release arch=x86_64
```

The DLL is written into `demo/addons/godot_terminal/bin/`. Open `demo/`
in Godot 4.3+ to test against the bundled demo project. SCons normally
locates MSVC automatically via `vswhere`; if it can't, run from a
*Developer Command Prompt for VS* (or after `vcvarsall.bat amd64`).

## Use from GDScript

The editor plugin auto-mounts a `Terminal` instance in the bottom panel.
You can also create one yourself at runtime:

```gdscript
var term := Terminal.new()
term.cols = 100
term.rows = 30
term.font_size = 14
add_child(term)

# Spawn a shell. Empty cwd = inherit; otherwise an absolute path.
term.start_process("powershell.exe", [], "C:/path/to/your/project")

# Pipe text directly to the child's stdin.
term.send_input("Get-Process | Select -First 5\r\n")

# Connect to lifecycle signals.
term.process_exited.connect(func(code): print("exited: ", code))
```

Selected API (see [`src/terminal.h`](src/terminal.h) for the full set):

| Member | Purpose |
|--------|---------|
| `start_process(exe, args, cwd)` | Spawn a child via ConPTY |
| `stop_process()` | Kill the child and detach the PTY |
| `send_input(text)` / `send_input_bytes(data)` | Write to child stdin |
| `write_text(s)` / `write_bytes(b)` | Inject bytes directly into the VT parser (no PTY) |
| `cols` / `rows` | Cell-grid size; resizing also resizes the ConPTY |
| `font` / `font_size` | Use a `SystemFont` or `FontFile`; monospace recommended |
| `scroll_to_bottom()` / `scroll_by(n)` / `clear_scrollback()` | Scrollback view controls |
| `set_max_scrollback(n)` | Default 5000 lines |
| Signals: `process_started`, `process_exited(exit_code)` | Child lifecycle |

## Status / roadmap

What works today (`v0.1.0`):

- [x] GDExtension scaffolding, Godot 4.3+ load
- [x] libvterm-driven cell rendering with full color/style data plumbed
- [x] ConPTY child process spawn + bidirectional I/O
- [x] Keyboard input mapping (arrows, F-keys, Ctrl/Alt, etc.)
- [x] Scrollback (5000 lines, mouse-wheel)
- [x] Shell `cwd` defaults to the open Godot project root

Planned next:

- [ ] Cursor blink + shape (block / bar / underline)
- [ ] Mouse-button forwarding to TUI apps (xterm mouse modes)
- [ ] Selection + clipboard copy/paste
- [ ] Auto-resize when the panel size changes (cols/rows from cell math)
- [ ] Bold / italic / underline glyph rendering (data already piped through)
- [ ] `claude-code` / `codex` compatibility pass — fix bugs surfaced
  by their richer TUI rendering
- [ ] macOS + Linux backends (`forkpty` / `posix_openpt`)

## License

MIT — see [LICENSE](LICENSE).

Bundled third-party code:

- **[godot-cpp](https://github.com/godotengine/godot-cpp)** — MIT
  (Godot Engine project)
- **[libvterm 0.3.3](https://www.leonerd.org.uk/code/libvterm/)** by
  Paul "LeoNerd" Evans — MIT (vendored under `thirdparty/libvterm/`)

## Acknowledgments

This plugin exists because libvterm is good at being a terminal, ConPTY
is good at faking one, and Godot exposes enough of its rendering API
to a Control node that you can paint cells fast enough not to notice.
