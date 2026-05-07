# godot-terminal

[English](README.md) · **中文**

> 在 Godot 编辑器里嵌入一个真正的终端 —— `cmd.exe`、`claude-code`、`codex`
> 任何 TUI 程序都可以直接跑，不用离开引擎。

为 Godot 4.3+ 编写的 C++ GDExtension，在编辑器底部面板加一个 **Terminal**
标签页。基于 Windows ConPTY + libvterm，所以任何现代命令行工具
（vim 风格应用、AI 编程助手、构建监视器、REPL）都能像在 Windows Terminal
里一样正常工作。

`Terminal` 类同时是个普通的 `Control` 节点，丢进运行时场景就能当游戏内
控制台用。

[![build](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/Azukibits/godot-terminal/actions/workflows/build.yml)

![Godot 编辑器底部面板里打开的 Terminal 标签页](docs/img1.png)

同一个面板里随便切换 shell 和 TUI 工具 —— 不用开额外窗口，也不用切上下文：

![claude-code 会话刚结束、紧接着在同一面板跑 OpenAI Codex](docs/img2.png)

## 功能

- 真正的 ANSI / xterm 终端模拟，由 **libvterm 0.3.3** 驱动
  （truecolor、256 色、调色板色、粗体/斜体/下划线、备用屏幕、
  鼠标交互的程序）
- 通过 Windows **ConPTY** 启动子进程
  （`cmd.exe`、`powershell.exe`、`claude-code`、`codex`……）
- 完整键盘支持：方向键、`F1`–`F12`、`Ctrl/Alt` 组合键、`Tab`、
  `Esc`、`Backspace`、功能键
- 5000 行 **滚动历史**，鼠标滚轮翻阅；按键自动跳回最新位置
- 鼠标滚轮滚动；**Shift+滚轮** 翻页
- 子进程默认在你**当前打开的 Godot 项目根目录**启动，
  AI 编程工具自然能识别正确的项目
- 编辑器之外没有运行时依赖（除了 Godot 4.3+ 与 Win10 1809+ 系统）

## 系统要求

- **Windows 10 1809 及以上**（ConPTY API）
- **Godot 4.3 及以上**

macOS / Linux 在路线图里。

## 快速安装（推荐）

1. 从 [Releases 页面](https://github.com/Azukibits/godot-terminal/releases)
   下载最新的 **`godot_terminal-vX.Y.Z-win64.zip`**。
2. 解压。把里面的 `godot_terminal/` 文件夹复制到你 Godot 项目的
   `addons/` 目录下，最终路径形如
   `your_project/addons/godot_terminal/`。
3. 在 Godot 里打开 *项目 → 项目设置 → 插件* 勾选 **godot_terminal**
   启用。
4. 编辑器底部面板会多一个 **Terminal** 标签页（在 *输出* / *调试器* /
   *音频* 旁边）。点开，点一下面板内部获取焦点，开始打字。

## 从源码构建

适合想改插件源码、或为某个尚未发布的 Godot/Windows 组合自行构建的开发者。

依赖：

- Visual Studio 2019/2022，勾选 *使用 C++ 的桌面开发* 工作负载
- Python 3.8+
- SCons 4.x（`pip install scons`）

```sh
git clone --recurse-submodules https://github.com/Azukibits/godot-terminal.git
cd godot-terminal
scons platform=windows target=template_release arch=x86_64
```

dll 会被写到 `demo/addons/godot_terminal/bin/`。直接用 Godot 4.3+
打开 `demo/` 即可对照测试。SCons 通常能通过 `vswhere` 自动找到 MSVC；
如果失败，请从 *Developer Command Prompt for VS* 启动（或先跑
`vcvarsall.bat amd64`）。

## 在 GDScript 中使用

启用插件后，编辑器底部面板会自动挂载一个 `Terminal` 实例。也可以在
运行时自己创建：

```gdscript
var term := Terminal.new()
term.cols = 100
term.rows = 30
term.font_size = 14
add_child(term)

# spawn 一个 shell。cwd 留空 = 继承父进程；否则填绝对路径。
term.start_process("powershell.exe", [], "C:/path/to/your/project")

# 直接往子进程的 stdin 写文本。
term.send_input("Get-Process | Select -First 5\r\n")

# 监听生命周期信号。
term.process_exited.connect(func(code): print("exited: ", code))
```

部分 API（完整列表见 [`src/terminal.h`](src/terminal.h)）：

| 成员 | 用途 |
|------|------|
| `start_process(exe, args, cwd)` | 经 ConPTY 启动子进程 |
| `stop_process()` | 杀掉子进程并释放 PTY |
| `send_input(text)` / `send_input_bytes(data)` | 写入子进程 stdin |
| `write_text(s)` / `write_bytes(b)` | 直接喂字节给 VT 解析器（不经 PTY） |
| `cols` / `rows` | 字符网格尺寸；改变时同步 resize ConPTY |
| `font` / `font_size` | 接受 `SystemFont` 或 `FontFile`；建议等宽字体 |
| `scroll_to_bottom()` / `scroll_by(n)` / `clear_scrollback()` | 滚动视图控制 |
| `set_max_scrollback(n)` | 默认 5000 行 |
| 信号 `process_started`, `process_exited(exit_code)` | 子进程生命周期 |

## 状态 / 路线图

`v0.1.0` 已实现：

- [x] GDExtension 骨架，可被 Godot 4.3+ 加载
- [x] libvterm 驱动的字符网格渲染（颜色、属性已全数贯通）
- [x] ConPTY 子进程 spawn + 双向 I/O
- [x] 键盘输入映射（方向键、F 键、Ctrl/Alt 等）
- [x] 滚动历史（5000 行、鼠标滚轮）
- [x] shell 默认 `cwd` 指向当前 Godot 项目根

接下来计划：

- [ ] 光标闪烁 + 形状（block / bar / underline）
- [ ] 鼠标按键转发到 TUI 程序（xterm 鼠标模式）
- [ ] 选区 + 剪贴板复制粘贴
- [ ] 面板尺寸变化时自动按 cell 大小算 cols/rows 重新 resize
- [ ] 粗体 / 斜体 / 下划线字形渲染（数据已贯通，差画的部分）
- [ ] `claude-code` / `codex` 兼容性打磨 —— 修它们更复杂的 TUI 渲染暴露的问题
- [ ] macOS + Linux 后端（`forkpty` / `posix_openpt`）

## 许可

本项目使用 MIT 协议 —— 见 [LICENSE](LICENSE)。

随项目附带的第三方代码：

- **[godot-cpp](https://github.com/godotengine/godot-cpp)** — MIT
  （Godot Engine 项目）
- **[libvterm 0.3.3](https://www.leonerd.org.uk/code/libvterm/)** —
  作者 Paul "LeoNerd" Evans —— MIT（在 `thirdparty/libvterm/` 下随源码分发）

## 致谢

这个插件能跑起来，是因为 libvterm 把"模拟终端"这件事做得很好，
ConPTY 把"伪装成终端"这件事做得很好，而 Godot 给一个 Control 节点
开放的渲染 API 也足够丰富，可以一帧画出几千个字符的 cell 而不卡。
