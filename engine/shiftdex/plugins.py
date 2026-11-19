# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugins: shareable sets of changes that can be applied to a project.

A plugin is a folder (or a .sdplugin zip of that folder) holding a
plugin.json and, optionally, the files it ships. Plugins are declarative:
they copy files, delete files and replace text. They never run code, so
applying one from another developer cannot do anything outside the
project it is applied to. The format is documented in docs/PLUGINS.md.
"""

import fnmatch
import os
import re
import shutil
import time
import zipfile
from pathlib import Path

from . import config
from .project import is_within, safe_relpath
from .runner import JobError

FORMAT = 1
EXTENSION = ".sdplugin"
_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{1,63}")
_OPS = ("copy", "delete", "replace")

# What kind of code a plugin changes. Every plugin has exactly one code
# type, and the app always shows it before a plugin is installed or applied.
#   smali  the plugin edits smali, the editable form of an APK's code
#   java   the plugin edits Java code: .class or .java files of a JAR
#   none   the plugin leaves code alone (resources, assets, manifest)
CODE_TYPES = ("smali", "java", "none")
CODE_LABELS = {"smali": "Smali", "java": "Java", "none": "No code"}
_SMALI_HINT = re.compile(r"(^|/)smali(/|$)|\.smali$", re.IGNORECASE)
_JAVA_HINT = re.compile(r"\.(class|java|kt)$", re.IGNORECASE)


def guess_code(paths, kind="any"):
    """Code type implied by a list of project paths and patterns."""
    smali = any(_SMALI_HINT.search(p) for p in paths)
    java = any(_JAVA_HINT.search(p) for p in paths)
    if smali and java:
        # an APK's code is smali; class files in one are just payload
        return "java" if kind == "jar" else "smali"
    if smali:
        return "smali"
    if java:
        return "java"
    return "none"
_MAX_TEXT = 8 * 1024 * 1024


class PluginError(JobError):
    pass


class Plugin:
    def __init__(self, directory, manifest):
        self.dir = Path(directory)
        self.manifest = manifest

    @property
    def id(self):
        return self.manifest["id"]

    @property
    def name(self):
        return self.manifest.get("name") or self.id

    @property
    def version(self):
        return str(self.manifest.get("version", ""))

    @property
    def author(self):
        return str(self.manifest.get("author", ""))

    @property
    def description(self):
        return str(self.manifest.get("description", ""))

    @property
    def target(self):
        target = self.manifest.get("target") or {}
        return {"type": target.get("type", "any"), "package": target.get("package", "")}

    @property
    def operations(self):
        return self.manifest.get("operations", [])

    @property
    def code(self):
        """'smali', 'java' or 'none'. Worked out from the operations when
        the plugin does not say."""
        declared = self.manifest.get("code")
        if declared in CODE_TYPES:
            return declared
        paths = []
        for op in self.operations:
            if op["op"] == "copy":
                source = self.dir / safe_relpath(op["from"])
                if source.is_dir():
                    for base, _dirs, files in os.walk(source):
                        rel = os.path.relpath(base, source).replace(os.sep, "/")
                        paths += [f"{op.get('to', '')}/{rel}/{name}" for name in files]
                else:
                    paths += [op["from"], op.get("to", "")]
            else:
                paths.append(op.get("path") or op.get("files") or "")
        return guess_code(paths, self.target["type"])

    @property
    def code_label(self):
        return CODE_LABELS[self.code]

    def summary(self):
        counts = {}
        for op in self.operations:
            counts[op["op"]] = counts.get(op["op"], 0) + 1
        words = {"copy": "copy", "delete": "delete", "replace": "replace"}
        return ", ".join(f"{n} {words[k]}" for k, n in counts.items()) or "no operations"


# -- validation --------------------------------------------------------------

def validate_manifest(manifest):
    """Check a parsed plugin.json. Raises PluginError describing the problem."""
    if not isinstance(manifest, dict):
        raise PluginError("plugin.json must contain a JSON object.")
    fmt = manifest.get("format", 1)
    if not isinstance(fmt, int) or fmt < 1:
        raise PluginError("plugin.json has an invalid 'format'.")
    if fmt > FORMAT:
        raise PluginError("This plugin was made for a newer version of ShiftDex.")
    plugin_id = manifest.get("id")
    if not isinstance(plugin_id, str) or not _ID.fullmatch(plugin_id):
        raise PluginError("Plugin 'id' must be 2-64 characters: letters, digits, dot, dash, underscore.")
    if not isinstance(manifest.get("name", ""), str):
        raise PluginError("Plugin 'name' must be text.")
    target = manifest.get("target", {})
    if not isinstance(target, dict) or target.get("type", "any") not in ("any", "apk", "jar"):
        raise PluginError("Plugin 'target.type' must be 'apk', 'jar' or 'any'.")
    if not isinstance(target.get("package", ""), str):
        raise PluginError("Plugin 'target.package' must be text.")
    if manifest.get("code", "none") not in CODE_TYPES:
        raise PluginError("Plugin 'code' must be 'smali', 'java' or 'none'.")
    operations = manifest.get("operations")
    if not isinstance(operations, list):
        raise PluginError("Plugin 'operations' must be a list.")
    for index, op in enumerate(operations, 1):
        where = f"Operation {index}"
        if not isinstance(op, dict) or op.get("op") not in _OPS:
            raise PluginError(f"{where}: unknown operation {op.get('op') if isinstance(op, dict) else op!r}.")
        kind = op["op"]
        try:
            if kind == "copy":
                _text(op, "from", where)
                safe_relpath(op["from"])
                safe_relpath(_text(op, "to", where, allow_empty=True))
            elif kind == "delete":
                if not safe_relpath(_text(op, "path", where)):
                    raise PluginError(f"{where}: refusing to delete the whole project.")
            elif kind == "replace":
                if not safe_relpath(_text(op, "files", where)):
                    raise PluginError(f"{where}: 'files' is empty.")
                find = _text(op, "find", where)
                _text(op, "replace", where, allow_empty=True)
                if op.get("regex"):
                    try:
                        re.compile(find)
                    except re.error as exc:
                        raise PluginError(f"{where}: invalid regular expression ({exc}).") from exc
        except PluginError:
            raise
        except JobError as exc:
            raise PluginError(f"{where}: {exc}") from exc
    return manifest


def _text(op, key, where, allow_empty=False):
    value = op.get(key, "" if allow_empty else None)
    if not isinstance(value, str) or (not value and not allow_empty):
        raise PluginError(f"{where}: '{key}' is missing.")
    return value


def load(directory):
    directory = Path(directory)
    manifest = config.read_json(directory / "plugin.json")
    if manifest is None:
        raise PluginError("plugin.json is missing or is not valid JSON.")
    return Plugin(directory, validate_manifest(manifest))


def list_installed():
    """Returns (plugins, broken) where broken is a list of (folder, reason)."""
    root = config.plugins_dir()
    plugins, broken = [], []
    if root.is_dir():
        for child in sorted(root.iterdir()):
            if not child.is_dir() or child.name.startswith("."):
                continue
            try:
                plugins.append(load(child))
            except PluginError as exc:
                broken.append((child.name, str(exc)))
    plugins.sort(key=lambda p: p.name.lower())
    return plugins, broken


# -- install / export / remove -----------------------------------------------

def install_file(path):
    """Install a .sdplugin archive, replacing an older copy of the same id."""
    path = Path(path)
    root = config.plugins_dir()
    root.mkdir(parents=True, exist_ok=True)
    staging = root / f".incoming-{os.getpid()}"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    try:
        try:
            with zipfile.ZipFile(path) as archive:
                names = [i.filename for i in archive.infolist() if not i.is_dir()]
                # tolerate archives that wrap everything in one top folder
                prefix = ""
                if "plugin.json" not in names:
                    tops = {n.split("/", 1)[0] for n in names if "/" in n}
                    if len(tops) == 1 and f"{next(iter(tops))}/plugin.json" in names:
                        prefix = next(iter(tops)) + "/"
                    else:
                        raise PluginError("This is not a ShiftDex plugin (no plugin.json inside).")
                for info in archive.infolist():
                    if info.is_dir() or not info.filename.startswith(prefix):
                        continue
                    rel = safe_relpath(info.filename[len(prefix):])
                    if not rel:
                        continue
                    target = staging / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as src, open(target, "wb") as out:
                        shutil.copyfileobj(src, out)
        except zipfile.BadZipFile as exc:
            raise PluginError("This file is not a ShiftDex plugin archive.") from exc
        plugin = load(staging)
        final = root / plugin.id
        shutil.rmtree(final, ignore_errors=True)
        os.replace(staging, final)
        return load(final)
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def export(plugin, dest):
    """Write the plugin as a .sdplugin archive."""
    dest = Path(dest)
    tmp = dest.with_name(dest.name + ".part")
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.write(plugin.dir / "plugin.json", "plugin.json")
            for base, dirs, files in os.walk(plugin.dir):
                dirs.sort()
                for name in sorted(files):
                    full = Path(base) / name
                    rel = full.relative_to(plugin.dir).as_posix()
                    if rel != "plugin.json":
                        archive.write(full, rel)
        os.replace(tmp, dest)
    finally:
        tmp.unlink(missing_ok=True)
    return dest


def remove(plugin):
    if not is_within(config.plugins_dir(), plugin.dir) or plugin.dir == config.plugins_dir():
        raise PluginError("Refusing to remove a folder outside the plugins directory.")
    shutil.rmtree(plugin.dir, ignore_errors=True)


# -- applying ----------------------------------------------------------------

def compatibility(plugin, project):
    """Reasons this plugin should not be applied to project (empty = fine)."""
    problems = []
    target = plugin.target
    if target["type"] != "any" and target["type"] != project.kind:
        problems.append(f"Made for {target['type'].upper()} projects, this one is {project.kind.upper()}.")
    pattern = target["package"]
    if pattern:
        package = project.meta.get("package", "")
        if not fnmatch.fnmatchcase(package, pattern):
            problems.append(f"Made for package {pattern}, this project is {package or 'unknown'}.")
    code = plugin.code
    if code == "smali":
        if project.kind != "apk":
            problems.append("A smali plugin: it changes smali code, which only APK projects have.")
        elif not (project.src / "smali").is_dir():
            problems.append("A smali plugin, but this APK was unpacked without smali code. "
                            "Turn on “Decode DEX code to smali” in Preferences and open the APK again.")
    elif code == "java" and project.kind != "jar":
        problems.append("A Java plugin: it changes Java class files, which only JAR projects have. "
                        "In an APK the Java view is read-only; its code is changed through smali.")
    return problems


def _matches(project, pattern):
    """Files and folders inside src/ matching a glob pattern."""
    pattern = safe_relpath(pattern)
    if not pattern:
        return []
    if not any(ch in pattern for ch in "*?["):
        target = project.src / pattern
        found = [target] if target.exists() or target.is_symlink() else []
    else:
        found = sorted(project.src.glob(pattern))
    return [p for p in found if is_within(project.src, p.parent)]


def _files(project, pattern):
    out = []
    for match in _matches(project, pattern):
        if match.is_symlink():
            continue
        if match.is_file():
            out.append(match)
        elif match.is_dir():
            for base, _dirs, files in os.walk(match):
                out += [Path(base) / name for name in sorted(files)]
    return out


def _replace_in(path, op, write):
    """Apply one replace operation to one file. Returns the number of hits."""
    try:
        if path.stat().st_size > _MAX_TEXT:
            return 0
        text = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError):
        return 0
    find, repl = op["find"], op.get("replace", "")
    if op.get("regex"):
        flags = re.MULTILINE | (re.IGNORECASE if op.get("ignore_case") else 0)
        new, hits = re.subn(find, repl, text, flags=flags)
    elif op.get("ignore_case"):
        new, hits = re.subn(re.escape(find), lambda _m: repl, text, flags=re.IGNORECASE)
    else:
        hits = text.count(find)
        new = text.replace(find, repl) if hits else text
    if hits and write and new != text:
        path.write_bytes(new.encode("utf-8"))
    return hits


def precheck(plugin, project):
    """Find problems before anything is modified."""
    problems = []
    copied = False
    for index, op in enumerate(plugin.operations, 1):
        kind = op["op"]
        if kind == "copy":
            source = plugin.dir / safe_relpath(op["from"])
            if not source.exists() or not is_within(plugin.dir, source):
                problems.append(f"Operation {index}: the plugin does not contain '{op['from']}'.")
            copied = True
        elif kind == "replace" and op.get("required", True) and not copied:
            # once files were copied in, earlier state says nothing about matches
            hits = sum(_replace_in(p, op, write=False) for p in _files(project, op["files"]))
            if not hits:
                label = op.get("name") or _clip(op["find"])
                problems.append(f"Operation {index}: nothing to replace for “{label}” in {op['files']}.")
    return problems


def apply(plugin, project, ctx):
    """Apply every operation of plugin to project. Returns a list of report lines."""
    problems = precheck(plugin, project)
    if problems:
        raise PluginError("The plugin does not fit this project:\n" + "\n".join(problems))
    report = []
    for index, op in enumerate(plugin.operations, 1):
        ctx.check()
        kind = op["op"]
        if kind == "copy":
            count = _copy(plugin.dir / safe_relpath(op["from"]), project, safe_relpath(op.get("to", "")), ctx)
            line = f"Copied {count} file(s)" + (f" to {op['to']}" if op.get("to") else "")
        elif kind == "delete":
            count = 0
            for match in _matches(project, op["path"]):
                if match == project.src:
                    continue
                if match.is_dir() and not match.is_symlink():
                    shutil.rmtree(match)
                else:
                    match.unlink(missing_ok=True)
                count += 1
            line = f"Deleted {count} item(s) matching {op['path']}"
        else:
            hits = files = 0
            for path in _files(project, op["files"]):
                ctx.check()
                found = _replace_in(path, op, write=True)
                if found:
                    hits += found
                    files += 1
            label = op.get("name") or _clip(op["find"])
            if not hits and op.get("required", True):
                raise PluginError(
                    f"Operation {index}: nothing to replace for “{label}”. "
                    "Earlier operations were already applied; restore the snapshot to undo them."
                )
            line = f"Replaced {hits} occurrence(s) in {files} file(s): {label}"
        ctx.log(line)
        report.append(line)
    project.meta.setdefault("applied_plugins", []).append({
        "id": plugin.id, "name": plugin.name, "version": plugin.version, "time": time.time(),
    })
    project.refresh_info()
    project.touch()
    return report


def _copy(source, project, to, ctx):
    count = 0
    if source.is_file():
        # a file copied to "" or to an existing folder keeps its name
        dest_rel = to
        if not to or (project.src / to).is_dir() or to.endswith("/"):
            dest_rel = f"{to}/{source.name}" if to else source.name
        pairs = [(source, dest_rel)]
    else:
        pairs = []
        for base, _dirs, files in os.walk(source):
            for name in sorted(files):
                full = Path(base) / name
                rel = full.relative_to(source).as_posix()
                pairs.append((full, f"{to}/{rel}" if to else rel))
    for full, rel in pairs:
        ctx.check()
        if full.is_symlink():
            continue
        target = project.path(rel)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_dir():
            raise PluginError(f"Cannot replace folder {rel} with a file.")
        shutil.copyfile(full, target)
        count += 1
    return count


def _clip(text, limit=48):
    text = " ".join(text.split())
    return text if len(text) <= limit else text[: limit - 1] + "…"


# -- creating ----------------------------------------------------------------

def create(info, project=None, files=(), deleted=(), rules=(), restrict_package=False, overwrite=False):
    """Build a plugin and install it.

    info:    {'id', 'name', 'version', 'author', 'description'}
    files:   project-relative paths whose current content is shipped
    deleted: project-relative paths the plugin removes
    rules:   replace operations ({'files', 'find', 'replace', 'regex', ...})
    """
    plugin_id = (info.get("id") or "").strip()
    if not _ID.fullmatch(plugin_id):
        raise PluginError("The plugin ID must be 2-64 characters: letters, digits, dot, dash, underscore.")
    root = config.plugins_dir()
    final = root / plugin_id
    if final.exists() and not overwrite:
        raise PluginError(f"A plugin with the ID {plugin_id} is already installed.")
    files = [safe_relpath(f) for f in files]
    deleted = [safe_relpath(d) for d in deleted]
    if not files and not deleted and not rules:
        raise PluginError("The plugin is empty. Include at least one change or replace rule.")

    target = {"type": project.kind if project else "any"}
    if restrict_package and project and project.meta.get("package"):
        target["package"] = project.meta["package"]

    operations = []
    if files:
        operations.append({"op": "copy", "from": "files", "to": ""})
    operations += [{"op": "delete", "path": rel} for rel in deleted if rel]
    for rule in rules:
        op = {"op": "replace", "files": rule["files"], "find": rule["find"], "replace": rule.get("replace", "")}
        for flag in ("regex", "ignore_case"):
            if rule.get(flag):
                op[flag] = True
        if not rule.get("required", True):
            op["required"] = False
        operations.append(op)

    code = info.get("code")
    if code not in CODE_TYPES:
        code = guess_code(files + deleted + [rule["files"] for rule in rules],
                          project.kind if project else "any")

    manifest = validate_manifest({
        "format": FORMAT,
        "id": plugin_id,
        "name": (info.get("name") or plugin_id).strip(),
        "version": (info.get("version") or "1.0").strip(),
        "author": (info.get("author") or "").strip(),
        "description": (info.get("description") or "").strip(),
        "code": code,
        "target": target,
        "operations": operations,
    })

    root.mkdir(parents=True, exist_ok=True)
    staging = root / f".new-{os.getpid()}"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    try:
        for rel in files:
            source = project.path(rel)
            if not source.is_file():
                raise PluginError(f"{rel} no longer exists in the project.")
            target_file = staging / "files" / rel
            target_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target_file)
        config.write_json(staging / "plugin.json", manifest)
        shutil.rmtree(final, ignore_errors=True)
        os.replace(staging, final)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return load(final)
