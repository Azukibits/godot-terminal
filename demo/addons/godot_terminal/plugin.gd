@tool
extends EditorPlugin

# Hosts a Terminal instance as a tab in Godot's bottom panel.

const _PANEL_TITLE := "Terminal"

var _terminal: Control = null

func _enter_tree() -> void:
	if not ClassDB.class_exists("Terminal"):
		push_error("[godot_terminal] Terminal class not registered — DLL not loaded?")
		return

	_terminal = ClassDB.instantiate("Terminal")
	if _terminal == null:
		push_error("[godot_terminal] Failed to instantiate Terminal")
		return

	_terminal.custom_minimum_size = Vector2(0, 320)
	_terminal.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_terminal.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var sf := SystemFont.new()
	sf.font_names = PackedStringArray([
		"Cascadia Mono",
		"Cascadia Code",
		"Consolas",
		"Menlo",
		"SF Mono",
		"DejaVu Sans Mono",
		"Liberation Mono",
		"Courier New",
		"monospace",
	])
	_terminal.font = sf

	add_control_to_bottom_panel(_terminal, _PANEL_TITLE)

	_terminal.process_exited.connect(_on_process_exited)
	_terminal.process_started.connect(_on_process_started)

	# Defer process spawn to next idle frame so the node is fully in the tree.
	call_deferred("_spawn_default_shell")

func _exit_tree() -> void:
	if _terminal != null:
		if _terminal.has_method("stop_process"):
			_terminal.stop_process()
		remove_control_from_bottom_panel(_terminal)
		_terminal.queue_free()
		_terminal = null

# Pick a shell appropriate for the host OS. On Unix, $SHELL is preferred so
# users get whatever they configured (zsh, fish, ...).
func _default_shell() -> Dictionary:
	var os_name: String = OS.get_name()
	if os_name == "Windows":
		return {"exe": "cmd.exe", "args": PackedStringArray()}

	var sh: String = OS.get_environment("SHELL")
	if sh.is_empty():
		# macOS defaults to /bin/zsh since Catalina; Linux varies but bash is
		# almost always present.
		sh = "/bin/zsh" if os_name == "macOS" else "/bin/bash"

	# -l makes the shell read login dotfiles so PATH includes Homebrew /
	# user-installed CLIs. Harmless if the shell ignores it.
	return {"exe": sh, "args": PackedStringArray(["-l"])}

func _spawn_default_shell() -> void:
	if _terminal == null:
		return
	# Run the shell in the open Godot project's root so AI coding tools
	# (claude-code, codex, ...) inspect that project — not the host editor's
	# parent process directory.
	var project_cwd: String = ProjectSettings.globalize_path("res://")
	var shell: Dictionary = _default_shell()
	print("[godot_terminal] spawning ", shell.exe, " with cwd=", project_cwd)
	var ok: bool = _terminal.start_process(shell.exe, shell.args, project_cwd)
	if not ok:
		push_error("[godot_terminal] ", shell.exe, " failed to start")
		return
	_terminal.grab_focus()

func _on_process_started() -> void:
	print("[godot_terminal] process started")

func _on_process_exited(exit_code: int) -> void:
	print("[godot_terminal] process exited with code ", exit_code)
