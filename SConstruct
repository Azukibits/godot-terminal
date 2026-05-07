#!/usr/bin/env python
# Build script for the godot_terminal GDExtension.
# Usage:
#   scons platform=windows target=template_debug arch=x86_64
#   scons platform=windows target=template_release arch=x86_64
#
# Run from a "Developer Command Prompt for VS" (or after vcvarsall) so cl.exe is in PATH.
# On most machines SCons auto-detects MSVC via vswhere — plain Git Bash works.

import os
import sys

env = SConscript("godot-cpp/SConstruct")

# --- Header search paths -------------------------------------------------------
env.Append(CPPPATH=[
    "src/",
    "thirdparty/libvterm/include",
    "thirdparty/libvterm/src",  # vterm_internal.h, rect.h, utf8.h, *.inc
])

# --- C++ sources (this extension) ---------------------------------------------
sources = Glob("src/*.cpp")

# --- libvterm C sources -------------------------------------------------------
libvterm_sources = Glob("thirdparty/libvterm/src/*.c")

# Quiet down MSVC about libvterm's C99 idioms.
if env["platform"] == "windows" and env.get("CC") == "cl":
    # /wd4267 size_t->int, /wd4244 conversion, /wd4146 unary minus on unsigned,
    # /wd4018 signed/unsigned mismatch, /wd4101 unreferenced local.
    env.Append(CFLAGS=[
        "/wd4267", "/wd4244", "/wd4146", "/wd4018", "/wd4101",
        "/wd4334",  # 32-bit shift result implicitly converted
    ])
    # libvterm uses inline functions in headers; nothing extra needed for that.

# --- Output -------------------------------------------------------------------
suffix = "{}.{}.{}".format(env["platform"], env["target"], env["arch"])
addon_bin_dir = "demo/addons/godot_terminal/bin"

if env["platform"] == "windows":
    library_name = "godot_terminal.{}.dll".format(suffix)
elif env["platform"] in ("macos", "linux"):
    # Reserved for future cross-platform backends; PTY layer is Windows-only today.
    library_name = "libgodot_terminal.{}.{}".format(
        suffix,
        "dylib" if env["platform"] == "macos" else "so",
    )
else:
    print("Unsupported platform: {}".format(env["platform"]))
    Exit(1)

library = env.SharedLibrary(
    target=os.path.join(addon_bin_dir, library_name),
    source=sources + libvterm_sources,
)

Default(library)
