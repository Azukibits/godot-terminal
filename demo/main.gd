extends Control

# This scene is intentionally minimal — the real surface for godot_terminal
# is the editor's bottom panel (see addons/godot_terminal/plugin.gd).
# This scene just exists so `Run` doesn't fail on an empty project.

func _ready() -> void:
	print("godot_terminal demo: open the 'Terminal' tab in the bottom panel of the editor.")
