# SPDX-License-Identifier: GPL-3.0-or-later
"""Paths and persistent settings."""

import copy
import json
import os
from pathlib import Path

THEMES = ("system", "light", "dark", "amoled")

DEFAULTS = {
    "setup_done": False,
    "theme": "system",
    "workspace_dir": "",
    "window_width": 1180,
    "window_height": 760,
    "sidebar_width": 280,
    # tools
    "java_path": "",
    "java_heap": "",
    # unpacking / building
    "decode_type": "xml",
    "decode_smali": True,
    "sign_after_build": True,
    "keystore_path": "",
    "keystore_alias": "",
    "keystore_pass": "",
    "key_pass": "",
    "jar_strip_signatures": True,
    "last_export_dir": "",
    # backups / plugins
    "snapshot_before_plugin": True,
    "snapshot_before_restore": True,
    "backup_on_save": False,
    # editor
    "editor_font_size": 11,
    "editor_wrap": False,
    "editor_line_numbers": True,
    "editor_highlight_line": True,
    "editor_tab_width": 4,
    "editor_spaces": True,
    # action name -> accelerator, only entries the user changed
    "keymap": {},
}


def _xdg(var, fallback):
    value = os.environ.get(var, "")
    return Path(value) if value and os.path.isabs(value) else Path.home() / fallback


def config_dir():
    return _xdg("XDG_CONFIG_HOME", ".config") / "shiftdex"


def data_dir():
    return _xdg("XDG_DATA_HOME", ".local/share") / "shiftdex"


def tools_dir():
    return data_dir() / "tools"


def plugins_dir():
    return data_dir() / "plugins"


def keys_dir():
    return data_dir() / "keys"


def default_workspace():
    return data_dir() / "projects"


def write_json(path, payload):
    """Write JSON through a temp file so a crash never leaves a half file."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    os.replace(tmp, path)


def read_json(path, fallback=None):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return fallback


class Settings:
    def __init__(self, path=None):
        self.path = Path(path) if path else config_dir() / "config.json"
        self._values = copy.deepcopy(DEFAULTS)
        self._listeners = []
        self.load()

    def load(self):
        stored = read_json(self.path, {})
        if not isinstance(stored, dict):
            return
        for key, default in DEFAULTS.items():
            value = stored.get(key)
            # bool is an int subclass, keep the two apart
            if value is not None and type(value) is type(default):
                self._values[key] = value
        if self._values["theme"] not in THEMES:
            self._values["theme"] = DEFAULTS["theme"]

    def save(self):
        write_json(self.path, self._values)

    def get(self, key):
        return self._values[key]

    def set(self, key, value):
        if key not in DEFAULTS:
            raise KeyError(key)
        if self._values[key] == value:
            return
        self._values[key] = value
        self.save()
        for listener in list(self._listeners):
            listener(key, value)

    def connect(self, listener):
        """listener(key, value) is called after every change."""
        self._listeners.append(listener)

    def disconnect(self, listener):
        if listener in self._listeners:
            self._listeners.remove(listener)

    @property
    def workspace(self):
        custom = self._values["workspace_dir"]
        return Path(custom) if custom else default_workspace()
