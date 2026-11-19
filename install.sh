#!/bin/sh
# ShiftDex installer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the build requirements with your distribution's package manager,
# builds ShiftDex from this folder and installs it.
#
#   ./install.sh              install for the current user  (~/.local)
#   ./install.sh --system     install for all users         (/usr/local)
#   ./install.sh --help       all options

set -eu

PREFIX="$HOME/.local"
INSTALL_DEPS=1
ASSUME_YES=0

usage() {
    cat <<'USAGE'
Usage: ./install.sh [options]

  --user          Install for the current user into ~/.local (default)
  --system        Install for all users into /usr/local (asks for sudo)
  --prefix DIR    Install into DIR instead
  --no-deps       Do not install packages; only build and install ShiftDex
  -y, --yes       Do not ask before installing packages
  -h, --help      Show this help

Supported: Arch Linux, Debian, Ubuntu, Fedora, openSUSE and distributions
based on them. Remove ShiftDex again with ./uninstall.sh.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --user) PREFIX="$HOME/.local" ;;
        --system) PREFIX="/usr/local" ;;
        --prefix)
            [ $# -ge 2 ] || { echo "error: --prefix needs a folder" >&2; exit 2; }
            PREFIX="$2"; shift ;;
        --prefix=*) PREFIX="${1#--prefix=}" ;;
        --no-deps) INSTALL_DEPS=0 ;;
        -y|--yes) ASSUME_YES=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

say()  { printf '\n==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

SRC_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ -f "$SRC_DIR/CMakeLists.txt" ] || die "run this script from the ShiftDex source folder"
case "$PREFIX" in
    /*) ;;
    *) PREFIX="$(pwd)/$PREFIX" ;;
esac

# Run a command as root: directly when we are root, through sudo otherwise.
as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif have sudo; then
        sudo "$@"
    elif have doas; then
        doas "$@"
    else
        die "this step needs root, but neither sudo nor doas is installed: $*"
    fi
}

# ------------------------------------------------------------- distribution

detect_family() {
    ids=""
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        ids=$(. /etc/os-release; printf '%s %s' "${ID:-}" "${ID_LIKE:-}")
    fi
    for id in $ids; do
        case "$id" in
            arch|archlinux|manjaro|endeavouros|cachyos|garuda|artix) echo arch; return ;;
            debian|ubuntu|linuxmint|pop|raspbian|devuan) echo debian; return ;;
            fedora|rhel|centos|nobara) echo fedora; return ;;
            opensuse*|suse|sles) echo suse; return ;;
        esac
    done
    # unknown name: go by the package manager that is present
    if have pacman; then echo arch
    elif have apt-get; then echo debian
    elif have dnf; then echo fedora
    elif have zypper; then echo suse
    else echo unknown
    fi
}

# A JDK is needed to build the Java helper; any version from 11 on will do.
have_jdk() {
    have javac || return 1
    version=$(javac -version 2>&1 | sed -n 's/^javac \([0-9]*\).*/\1/p')
    [ -n "$version" ] && [ "$version" -ge 11 ]
}

install_dependencies() {
    family=$(detect_family)
    jdk=""
    case "$family" in
        arch)
            have_jdk || jdk="jdk-openjdk"
            packages="base-devel cmake qt6-base qt6-svg qt6-imageformats python $jdk"
            command="pacman -S --needed"
            [ "$ASSUME_YES" -eq 1 ] && command="$command --noconfirm"
            ;;
        debian)
            have_jdk || jdk="default-jdk-headless"
            packages="build-essential cmake qt6-base-dev qt6-svg-dev qt6-image-formats-plugins libgl1-mesa-dev python3 $jdk"
            command="apt-get install"
            [ "$ASSUME_YES" -eq 1 ] && command="$command -y"
            ;;
        fedora)
            have dnf || die "dnf was not found. On image-based Fedora (Silverblue, Kinoite), build inside a toolbox and run ./install.sh there."
            have_jdk || jdk="java-latest-openjdk-devel"
            packages="gcc-c++ make cmake qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats python3 $jdk"
            command="dnf install"
            [ "$ASSUME_YES" -eq 1 ] && command="$command -y"
            ;;
        suse)
            have_jdk || jdk="java-21-openjdk-devel"
            packages="gcc-c++ make cmake qt6-base-devel qt6-svg-devel qt6-imageformats python3 $jdk"
            command="zypper install"
            [ "$ASSUME_YES" -eq 1 ] && command="zypper --non-interactive install"
            ;;
        *)
            warn "this distribution is not known to the installer."
            warn "Install these yourself, then run ./install.sh --no-deps:"
            warn "  a C and C++ compiler, CMake 3.19+, Qt 6.4+ (Widgets, Svg, image formats),"
            warn "  Python 3.9+, and a JDK 11+"
            return 0
            ;;
    esac

    say "Installing build requirements ($family)"
    echo "    $command $packages"
    if [ "$ASSUME_YES" -eq 0 ] && [ -t 0 ]; then
        printf 'Continue? [Y/n] '
        read -r answer
        case "$answer" in
            n|N|no|No|NO) die "stopped. Use --no-deps to skip this step." ;;
        esac
    fi
    if [ "$family" = debian ]; then
        as_root apt-get update
    fi
    # shellcheck disable=SC2086
    as_root $command $packages
}

# ------------------------------------------------------------------- build

check_tools() {
    missing=""
    have cmake || missing="$missing cmake"
    have c++ || have g++ || have clang++ || missing="$missing c++-compiler"
    have python3 || missing="$missing python3"
    have_jdk || missing="$missing jdk(11+)"
    [ -z "$missing" ] || die "still missing after installing packages:$missing"
}

build() {
    say "Building ShiftDex"
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
    cmake -S "$SRC_DIR" -B "$SRC_DIR/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_TESTING=OFF
    cmake --build "$SRC_DIR/build" --parallel "$jobs"
}

install_files() {
    say "Installing into $PREFIX"
    runner=""
    probe="$PREFIX"
    while [ ! -d "$probe" ]; do probe=$(dirname "$probe"); done
    [ -w "$probe" ] || runner="as_root"

    $runner cmake --install "$SRC_DIR/build"
    # kept next to the app so ./uninstall.sh knows exactly what to remove
    $runner cp "$SRC_DIR/build/install_manifest.txt" "$PREFIX/lib/shiftdex/install_manifest.txt"

    # let the desktop pick up the launcher, icon and file associations
    if have update-desktop-database; then
        $runner update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    fi
    # Refresh the icon cache where one is in use. Creating a cache in a folder
    # that had none would hide icons that other programs add there later, so
    # in that case the folder is only touched to make desktops look again.
    if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] && have gtk-update-icon-cache; then
        $runner gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
    else
        $runner touch "$PREFIX/share/icons/hicolor" 2>/dev/null || true
    fi
}

# APKEditor is what unpacks and builds APKs. Fetch it now so the app is
# ready to use; if this fails (no network), the app fetches it on first use.
fetch_apkeditor() {
    if [ "$(id -u)" -eq 0 ]; then
        # tools are per user; root's copy would be of no use to anyone
        return 0
    fi
    have java || return 0
    say "Fetching the latest APKEditor"
    if PYTHONPATH="$PREFIX/lib/shiftdex/engine" python3 -m shiftdex.cli install-apkeditor; then
        :
    else
        warn "APKEditor could not be downloaded now. ShiftDex will fetch it when you first open an APK."
    fi
}

finish() {
    say "ShiftDex is installed"
    echo "Start it from your application menu, or run: shiftdex"
    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *)
            echo
            echo "Note: $PREFIX/bin is not in your PATH, so the 'shiftdex' command will not"
            echo "be found in a terminal. Add this line to your shell profile:"
            echo "    export PATH=\"$PREFIX/bin:\$PATH\""
            echo "(fish: fish_add_path $PREFIX/bin)"
            ;;
    esac
    echo
    echo "To remove ShiftDex again: ./uninstall.sh$( [ "$PREFIX" = "$HOME/.local" ] || printf ' --prefix %s' "$PREFIX")"
}

[ "$INSTALL_DEPS" -eq 1 ] && install_dependencies
check_tools
build
install_files
fetch_apkeditor
finish
