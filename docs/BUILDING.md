# Building ShiftDex from source

Most people only need `./install.sh`, which installs the requirements,
builds and installs in one go (see the README). This page is for building
by hand, packaging, or working on ShiftDex itself.

## What gets built

ShiftDex is made of four parts in four languages, each used for what it is
good at:

| Folder | Language | What it is |
| --- | --- | --- |
| `ui/` | C++17, Qt 6 Widgets | The application: windows, editor, dialogs, themes, shortcuts. Becomes the `shiftdex` binary. |
| `native/` | C11 | Search over an unpacked project (file contents and names). Linked into the binary as a static library. |
| `jartool/` | Java 11 | Unpacks and packs JAR archives and disassembles `.class` files. Becomes `shiftdex-jartool.jar`. |
| `engine/` | Python 3 (standard library only) | Everything that works on projects: unpacking and building through APKEditor, signing, backups, plugins, renaming apps. Not compiled; run by the UI as a separate process and usable on its own from a terminal. |

CMake builds the C, C++ and Java parts and installs all four.

## Requirements

To build:

- a C11 and a C++17 compiler (GCC or Clang)
- CMake 3.19 or newer
- Qt 6.4 or newer: Widgets and Svg, with development files
- a JDK, version 11 or newer (`javac` and `jar`)

To run:

- Qt 6 runtime libraries, plus the Qt image formats plugin for WebP images
- Python 3.9 or newer
- Java 11 or newer. A JDK rather than a bare runtime lets ShiftDex show
  `.class` files as bytecode and create its signing key (`javap`,
  `keytool`).

APKEditor, the optional APK signer and the optional jadx (for the Java
view) are not build requirements. ShiftDex downloads them at run time into
`~/.local/share/shiftdex/tools`; `./install.sh` fetches APKEditor up front.

### Packages

Arch Linux, Manjaro, EndeavourOS, CachyOS:

```
sudo pacman -S --needed base-devel cmake qt6-base qt6-svg qt6-imageformats python jdk-openjdk
```

Debian 12 or newer, Ubuntu 24.04 or newer, Linux Mint 22 or newer:

```
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev qt6-image-formats-plugins \
    libgl1-mesa-dev python3 default-jdk-headless
```

Fedora:

```
sudo dnf install gcc-c++ make cmake qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats \
    python3 java-latest-openjdk-devel
```

openSUSE Tumbleweed:

```
sudo zypper install gcc-c++ make cmake qt6-base-devel qt6-svg-devel qt6-imageformats \
    python3 java-21-openjdk-devel
```

Ubuntu 22.04 ships Qt 6.2, which is too old.

## Build

```
git clone https://github.com/palazik/ShiftDex.git
cd ShiftDex
cmake -S . -B build
cmake --build build --parallel
```

The default build type is `Release`. For a debug build add
`-DCMAKE_BUILD_TYPE=Debug` to the first command. Add `-G Ninja` if you
prefer Ninja over Make.

## Run without installing

The build folder mirrors the installed layout, so the program runs straight
from it:

```
./build/bin/shiftdex
```

`build/lib/shiftdex/engine` is a link to the `engine/` folder in the source
tree. Changes to the Python code take effect the next time the app runs a
task, with no rebuild.

## Test

```
ctest --test-dir build --output-on-failure
```

This runs two suites:

- `native-search`: the C search library (`native/sdx_search_test.c`).
- `engine`: the Python engine (`tests/test_engine.py`): plugins, backups,
  manifest editing, and a JAR round trip through the Java tool.

The engine tests build small fake projects, so they need neither APKEditor
nor a real APK. To run them alone:

```
PYTHONPATH=engine python3 -m unittest discover -s tests -v
```

## Install

```
cmake --install build --prefix ~/.local
```

or, for all users:

```
sudo cmake --install build --prefix /usr/local
```

What goes where, relative to the prefix:

| Path | Content |
| --- | --- |
| `bin/shiftdex` | the application |
| `lib/shiftdex/shiftdex-jartool.jar` | the Java tool |
| `lib/shiftdex/engine/shiftdex/` | the Python engine |
| `share/applications/io.github.palazik.ShiftDex.desktop` | launcher and file associations |
| `share/icons/hicolor/…` | the icon |
| `share/metainfo/…` | AppStream metadata for software centres |
| `share/doc/shiftdex/` | this file, the plugin guide, README and licence |

The binary finds the rest through `../lib/shiftdex` relative to itself, so
the prefix can be moved as a whole. `./install.sh` does the same as the
commands above and additionally keeps a list of installed files for
`./uninstall.sh`.

### Notes for packagers

- Set the prefix at configure time (`-DCMAKE_INSTALL_PREFIX=/usr`) and use
  `DESTDIR=… cmake --install build` for staged installs.
- `-DBUILD_TESTING=OFF` skips the test program.
- Run-time dependencies: Qt 6 base and svg libraries, the Qt 6 image
  formats plugin, Python 3, a Java runtime. `apksigner` (Android SDK build
  tools) is an optional dependency; when it is on the `PATH`, ShiftDex uses
  it to sign APKs instead of downloading a signer.
- Nothing is downloaded at build time.

## Using the engine from a terminal

The engine is a normal command line program. From the source tree:

```
export PYTHONPATH=$PWD/engine
python3 -m shiftdex.cli --help
python3 -m shiftdex.cli status
python3 -m shiftdex.cli create ~/Downloads/app.apk
python3 -m shiftdex.cli build ~/.local/share/shiftdex/projects/app ~/app_mod.apk
```

For an installed copy, point `PYTHONPATH` at `<prefix>/lib/shiftdex/engine`.
Add `--json` before the command to get one JSON object per line, which is
how the UI talks to it: `{"log": …}` and `{"progress": …}` lines while it
works, then `{"ok": true, "result": …}` or `{"ok": false, "error": …}`.

## Working on the code

Some things that are useful to know:

- **Settings** live in one JSON file, `~/.config/shiftdex/config.json`. The
  UI writes it (`ui/Config.cpp`) and the engine reads it
  (`engine/shiftdex/config.py`). A new setting has to be added to the
  defaults in both files, with the same type.
- **Long tasks** never run in the UI process. `ui/JobDialog.cpp` starts an
  engine command, shows its output and can cancel it; the engine cleans up
  half-finished work when it receives SIGTERM.
- **Keyboard shortcuts** are declared in one table in `ui/Keymap.cpp`.
  Adding a row there makes the shortcut show up in Preferences; bind it to
  an action with `Keymap::bind`.
- **Themes** are in `ui/Theme.cpp`: a palette and a style sheet per theme,
  plus the editor colours used by `ui/Highlighter.cpp`.
- **The Java view** is a folder of jadx output next to the editable tree
  (`<project>/java`), made by `Project.decompile_java` in
  `engine/shiftdex/project.py`. The UI only ever opens it read-only; the
  mapping between a smali file and its Java file is in
  `Workspace::counterpart`.
- **The plugin format** is implemented in `engine/shiftdex/plugins.py` and
  documented in [PLUGINS.md](PLUGINS.md). Change both together.

### Checking the interface without a display

The binary has two hidden options for taking a picture of the interface,
which work with Qt's `offscreen` platform (no display or window needed):

```
QT_QPA_PLATFORM=offscreen ./build/bin/shiftdex --screenshot out.png \
    --view "dialog:shortcuts"
```

`--view` takes steps separated by `;`, run one after another before the
picture is taken:

| Step | Effect |
| --- | --- |
| `click:Text` | press the button with that text |
| `file:path` | open a project file (relative to the project) in the editor |
| `dialog:name` | open a dialog: `preferences`, `editor`, `building`, `tools`, `shortcuts`, `about`, `plugins`, `new-plugin`, `backups`, `identity` |
| `search:contents` / `search:names` | switch to the search panel |
| `find:find` / `find:replace` | open the find bar |
| `type:text` | put text into the focused field |
| `theme:name` | switch theme: `light`, `dark`, `amoled`, `system` |
| `codeview:smali` / `codeview:java` | switch the project's code view |
| `switchcode` | jump between the open class's smali and Java |

Combine with `--project <folder>` to start inside a project. To keep your
real settings and projects out of it, point `XDG_CONFIG_HOME` and
`XDG_DATA_HOME` at a scratch folder.

## Coding style

- C++: Qt conventions, four spaces, `m_` prefix for members, no exceptions.
- C: C11 with POSIX, four spaces, `sdx_` prefix for public names.
- Java: standard Java conventions, no dependencies outside the JDK.
- Python: PEP 8, lines up to 110 characters, standard library only.

Every source file carries the licence at its top: either the line
`SPDX-License-Identifier: GPL-3.0-or-later` or the full GPL notice.
