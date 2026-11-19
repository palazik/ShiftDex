#!/bin/sh
# ShiftDex uninstaller
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   ./uninstall.sh              remove a per-user install (~/.local)
#   ./uninstall.sh --system     remove a system install   (/usr/local)
#   ./uninstall.sh --purge      also delete your projects, plugins and settings

set -eu

PREFIX="$HOME/.local"
PURGE=0

usage() {
    cat <<'USAGE'
Usage: ./uninstall.sh [options]

  --user          Remove the install in ~/.local (default)
  --system        Remove the install in /usr/local (asks for sudo)
  --prefix DIR    Remove the install in DIR
  --purge         Also delete your projects, backups, plugins, downloaded
                  tools, signing key and settings
  -h, --help      Show this help
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
        --purge) PURGE=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

have() { command -v "$1" >/dev/null 2>&1; }

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif have sudo; then
        sudo "$@"
    elif have doas; then
        doas "$@"
    else
        echo "error: this step needs root, but neither sudo nor doas is installed" >&2
        exit 1
    fi
}

MANIFEST="$PREFIX/lib/shiftdex/install_manifest.txt"
if [ -f "$MANIFEST" ]; then
    runner=""
    [ -w "$PREFIX/lib/shiftdex" ] || runner="as_root"

    echo "Removing ShiftDex from $PREFIX"
    # the manifest lists every installed file, one per line
    while IFS= read -r file || [ -n "$file" ]; do
        case "$file" in
            "$PREFIX"/*) $runner rm -f -- "$file" ;;
        esac
    done < "$MANIFEST"
    # only ever remove folders that belong to ShiftDex alone
    $runner rm -rf -- "$PREFIX/lib/shiftdex" "$PREFIX/share/doc/shiftdex"

    if have update-desktop-database; then
        $runner update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    fi
    if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] && have gtk-update-icon-cache; then
        $runner gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
    fi
    echo "Done."
else
    echo "No ShiftDex install found in $PREFIX."
    echo "If you installed it elsewhere, pass --system or --prefix DIR."
fi

DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/shiftdex"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/shiftdex"

if [ "$PURGE" -eq 1 ]; then
    echo
    echo "This deletes your projects, backups, plugins, signing key and settings:"
    echo "    $DATA_DIR"
    echo "    $CONFIG_DIR"
    echo "Projects stored in a custom projects folder are not touched."
    printf 'Delete them? [y/N] '
    answer=""
    [ -t 0 ] && read -r answer
    case "$answer" in
        y|Y|yes|Yes|YES)
            rm -rf -- "$DATA_DIR" "$CONFIG_DIR"
            echo "Deleted."
            ;;
        *) echo "Kept." ;;
    esac
elif [ -d "$DATA_DIR" ] || [ -d "$CONFIG_DIR" ]; then
    echo
    echo "Your projects and settings were kept:"
    [ -d "$DATA_DIR" ] && echo "    $DATA_DIR"
    [ -d "$CONFIG_DIR" ] && echo "    $CONFIG_DIR"
    echo "Run ./uninstall.sh --purge to delete them too."
fi
