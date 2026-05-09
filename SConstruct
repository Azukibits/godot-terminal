#!/usr/bin/env python
# Build script for the godot_terminal GDExtension.
# Usage:
#   scons platform=windows target=template_debug arch=x86_64
#   scons platform=windows target=template_release arch=x86_64
#   scons platform=linux target=template_release arch=x86_64
#   scons platform=macos target=template_release arch=universal
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
# pty_windows.cpp / pty_unix.cpp self-guard with #ifdef _WIN32, but filtering
# them out of the source list also keeps platform-specific headers (windows.h,
# pty.h, util.h) from being parsed unnecessarily.
all_cpp = Glob("src/*.cpp")
sources = []
for s in all_cpp:
    name = os.path.basename(str(s))
    if env["platform"] == "windows" and name == "pty_unix.cpp":
        continue
    if env["platform"] in ("linux", "macos") and name == "pty_windows.cpp":
        continue
    sources.append(s)

# --- libvterm C sources -------------------------------------------------------
libvterm_sources = Glob("thirdparty/libvterm/src/*.c")

# --- Platform-specific compile / link flags -----------------------------------
if env["platform"] == "windows" and env.get("CC") == "cl":
    # Quiet down MSVC about libvterm's C99 idioms.
    # /wd4267 size_t->int, /wd4244 conversion, /wd4146 unary minus on unsigned,
    # /wd4018 signed/unsigned mismatch, /wd4101 unreferenced local.
    env.Append(CFLAGS=[
        "/wd4267", "/wd4244", "/wd4146", "/wd4018", "/wd4101",
        "/wd4334",  # 32-bit shift result implicitly converted
    ])

if env["platform"] == "linux":
    # forkpty / openpty live in libutil on glibc systems.
    env.Append(LIBS=["util"])

# --- Output -------------------------------------------------------------------
suffix = "{}.{}.{}".format(env["platform"], env["target"], env["arch"])
addon_bin_dir = "demo/addons/godot_terminal/bin"

if env["platform"] == "windows":
    library_name = "godot_terminal.{}.dll".format(suffix)
elif env["platform"] in ("macos", "linux"):
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
