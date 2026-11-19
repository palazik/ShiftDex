# SPDX-License-Identifier: GPL-3.0-or-later
"""Read and change what an APK calls itself: name, icon, package, version.

Works on a project decoded by APKEditor with `-t xml`:

    src/AndroidManifest.xml
    src/resources/package_N/package.json
    src/resources/package_N/res/values*/strings.xml
    src/resources/package_N/res/mipmap-*/ic_launcher.png

The manifest is edited as text rather than re-serialised so the rest of
the file stays exactly as APKEditor wrote it.
"""

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from xml.sax.saxutils import escape as xml_escape

from . import config
from .runner import JobError

ANDROID = "{http://schemas.android.com/apk/res/android}"
PACKAGE_RE = re.compile(r"[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+")
RASTER_EXTS = (".png", ".webp", ".jpg", ".jpeg")
DENSITIES = {"ldpi": 36, "mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}

_COMPONENTS = ("activity", "activity-alias", "service", "receiver", "provider")
_PERMISSION_TAGS = ("permission", "uses-permission", "uses-permission-sdk-23", "permission-group", "permission-tree")
_PERMISSION_ATTRS = ("permission", "readPermission", "writePermission", "permissionGroup")


@dataclass
class Identity:
    package: str = ""
    label: str = ""
    version_name: str = ""
    version_code: str = ""
    # '@string/app_name' style references (or literals) that hold the name
    label_values: list = field(default_factory=list)
    # (type, name) pairs such as ('mipmap', 'ic_launcher')
    icon_refs: list = field(default_factory=list)
    icon_file: str = ""  # project-relative path of an image to preview
    launcher_names: list = field(default_factory=list)


def _parse(project):
    try:
        return ET.parse(project.manifest_path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise JobError(f"AndroidManifest.xml could not be read: {exc}") from exc


def _is_launcher(component):
    for intent in component.findall("intent-filter"):
        actions = {a.get(ANDROID + "name") for a in intent.findall("action")}
        categories = {c.get(ANDROID + "name") for c in intent.findall("category")}
        if "android.intent.action.MAIN" in actions and categories & {
            "android.intent.category.LAUNCHER", "android.intent.category.LEANBACK_LAUNCHER",
        }:
            return True
    return False


def _res_dirs(project):
    resources = project.src / "resources"
    if not resources.is_dir():
        return []
    return sorted(p / "res" for p in resources.iterdir() if (p / "res").is_dir())


def _parse_ref(value):
    """'@mipmap/ic_launcher' -> ('mipmap', 'ic_launcher'); None for anything else."""
    match = re.fullmatch(r"@(?:\+)?(?:[\w.]+:)?([a-z]+)/([\w.]+)", value or "")
    return (match.group(1), match.group(2)) if match else None


def _resource_files(project, ref):
    """Every file providing a resource, across all configurations."""
    res_type, name = ref
    found = []
    for res in _res_dirs(project):
        for directory in sorted(res.iterdir()):
            if not directory.is_dir():
                continue
            if directory.name != res_type and not directory.name.startswith(res_type + "-"):
                continue
            for path in sorted(directory.iterdir()):
                if path.is_file() and path.name.split(".", 1)[0] == name:
                    found.append(path)
    return found


def _string_files(project):
    files = []
    for res in _res_dirs(project):
        for directory in sorted(res.iterdir()):
            if directory.is_dir() and (directory.name == "values" or directory.name.startswith("values-")):
                files += sorted(p for p in directory.glob("*.xml") if p.name != "public.xml")
    return files


def _string_pattern(name):
    return re.compile(r'(<string\b[^>]*\bname="' + re.escape(name) + r'"[^>]*>)(.*?)(</string>)', re.DOTALL)


def _resolve_string(project, name):
    """Value of a string resource in the default configuration."""
    pattern = _string_pattern(name)
    ordered = sorted(_string_files(project), key=lambda p: (p.parent.name != "values", str(p)))
    for path in ordered:
        try:
            match = pattern.search(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError):
            continue
        if match:
            return _unescape_string(match.group(2))
    return ""


def _unescape_string(raw):
    text = re.sub(r"<[^>]+>", "", raw)
    for entity, char in (("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&apos;", "'"), ("&amp;", "&")):
        text = text.replace(entity, char)
    return text.strip()


def _escape_string(value):
    # APKEditor stores the element text as-is; unlike aapt it does not
    # process backslash escapes, so only XML escaping is needed.
    return xml_escape(value)


def _best_raster(paths):
    rasters = [p for p in paths if p.suffix.lower() in RASTER_EXTS and not p.name.endswith(".9.png")]
    if not rasters:
        return None

    def weight(path):
        qualifiers = path.parent.name.split("-")[1:]
        return max((DENSITIES.get(q, 0) for q in qualifiers), default=40)

    return max(rasters, key=weight)


def _preview_file(project, refs):
    for ref in refs:
        files = _resource_files(project, ref)
        best = _best_raster(files)
        if best:
            return best
        # adaptive icon: fall back to its foreground layer
        for path in files:
            if path.suffix.lower() != ".xml":
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            for layer in ("foreground", "background"):
                match = re.search(r"<" + layer + r"\b[^>]*android:drawable=\"([^\"]+)\"", text)
                inner = _parse_ref(match.group(1)) if match else None
                best = _best_raster(_resource_files(project, inner)) if inner else None
                if best:
                    return best
    return None


def read(project):
    root = _parse(project)
    identity = Identity(
        package=root.get("package", ""),
        version_name=root.get(ANDROID + "versionName", ""),
        version_code=root.get(ANDROID + "versionCode", ""),
    )
    app = root.find("application")
    holders = []
    if app is not None:
        holders.append(app)
        for tag in ("activity", "activity-alias"):
            for component in app.findall(tag):
                if _is_launcher(component):
                    holders.append(component)
                    identity.launcher_names.append(component.get(ANDROID + "name", ""))
    for holder in holders:
        label = holder.get(ANDROID + "label")
        if label and label not in identity.label_values:
            identity.label_values.append(label)
        for attr in ("icon", "roundIcon"):
            ref = _parse_ref(holder.get(ANDROID + attr))
            if ref and ref not in identity.icon_refs:
                identity.icon_refs.append(ref)
    # what a launcher shows: the launcher activity's label wins over the app's
    shown = None
    for holder in holders[1:] + holders[:1]:
        shown = holder.get(ANDROID + "label")
        if shown:
            break
    if shown:
        ref = _parse_ref(shown)
        identity.label = _resolve_string(project, ref[1]) if ref and ref[0] == "string" else shown
    preview = _preview_file(project, identity.icon_refs)
    identity.icon_file = project.rel(preview) if preview else ""
    return identity


# -- manifest text editing ----------------------------------------------------

def _start_tags(text, tags):
    """Yield (match, tag_name) for every start tag whose name is in tags."""
    pattern = re.compile(r"<(" + "|".join(re.escape(t) for t in tags) + r')(?=[\s/>])(?:[^>"\']|"[^"]*"|\'[^\']*\')*>')
    for match in pattern.finditer(text):
        yield match


def _get_attr(tag_text, attr):
    match = re.search(r'(?<![\w:.-])' + re.escape(attr) + r'\s*=\s*(["\'])(.*?)\1', tag_text, re.DOTALL)
    return match.group(2) if match else None


def _set_attr(tag_text, attr, value):
    """Return tag_text with attr set (added when missing)."""
    quoted = xml_escape(value, {'"': "&quot;"})
    pattern = re.compile(r'((?<![\w:.-])' + re.escape(attr) + r'\s*=\s*)(["\'])(.*?)\2', re.DOTALL)
    if pattern.search(tag_text):
        return pattern.sub(lambda m: f'{m.group(1)}"{quoted}"', tag_text, count=1)
    head = re.match(r"<[\w:.-]+", tag_text)
    return f'{head.group(0)} {attr}="{quoted}"{tag_text[head.end():]}'


def _edit_tags(text, tags, editor):
    """Run editor(tag_text) -> tag_text over each matching start tag."""
    out, last = [], 0
    for match in _start_tags(text, tags):
        out.append(text[last:match.start()])
        out.append(editor(match.group(0)))
        last = match.end()
    out.append(text[last:])
    return "".join(out)


def _swap_prefix(value, old, new):
    if value == old:
        return new
    if value.startswith(old + "."):
        return new + value[len(old):]
    return value


def _rename_package(text, old, new, rename_refs):
    def qualify(tag_text):
        # relative class names are resolved against the package, so pin them
        # to the old one before it changes
        for attr in ("android:name", "android:targetActivity", "android:backupAgent"):
            value = _get_attr(tag_text, attr)
            if value is None or value.startswith("@"):
                continue
            if value.startswith("."):
                tag_text = _set_attr(tag_text, attr, old + value)
            elif "." not in value:
                tag_text = _set_attr(tag_text, attr, f"{old}.{value}")
        return tag_text

    text = _edit_tags(text, ("application",) + _COMPONENTS, qualify)
    text = _edit_tags(text, ("manifest",), lambda t: _set_attr(t, "package", new))
    if not rename_refs:
        return text

    def providers(tag_text):
        value = _get_attr(tag_text, "android:authorities")
        if value:
            renamed = ";".join(_swap_prefix(part, old, new) for part in value.split(";"))
            tag_text = _set_attr(tag_text, "android:authorities", renamed)
        return tag_text

    def permission_names(tag_text):
        value = _get_attr(tag_text, "android:name")
        if value:
            tag_text = _set_attr(tag_text, "android:name", _swap_prefix(value, old, new))
        return tag_text

    def permission_uses(tag_text):
        for attr in _PERMISSION_ATTRS:
            value = _get_attr(tag_text, "android:" + attr)
            if value:
                tag_text = _set_attr(tag_text, "android:" + attr, _swap_prefix(value, old, new))
        return tag_text

    text = _edit_tags(text, ("provider",), providers)
    text = _edit_tags(text, _PERMISSION_TAGS, permission_names)
    text = _edit_tags(text, ("application", "permission") + _COMPONENTS, permission_uses)
    return text


# -- applying changes ----------------------------------------------------------

def apply(project, label=None, package=None, icon_dir=None, version_name=None, version_code=None,
          all_locales=True, rename_refs=True):
    """Apply the given changes (None = leave alone). Returns report lines."""
    current = read(project)
    try:
        text = project.manifest_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise JobError(f"AndroidManifest.xml could not be read: {exc}") from exc
    original_text = text
    report = []

    if package is not None and package != current.package:
        package = package.strip()
        if not PACKAGE_RE.fullmatch(package):
            raise JobError("A package name needs at least two parts, like com.example.app, "
                           "each starting with a letter.")
        if not current.package:
            raise JobError("The manifest has no package attribute to change.")
        text = _rename_package(text, current.package, package, rename_refs)
        for res in _res_dirs(project):
            info_path = res.parent / "package.json"
            info = config.read_json(info_path)
            if isinstance(info, dict) and info.get("package_name") == current.package:
                info["package_name"] = package
                config.write_json(info_path, info)
        report.append(f"Package: {current.package} → {package}")

    if version_name is not None and version_name != current.version_name:
        text = _edit_tags(text, ("manifest",), lambda t: _set_attr(t, "android:versionName", version_name))
        report.append(f"Version name: {version_name}")
    if version_code is not None and str(version_code) != current.version_code:
        if not re.fullmatch(r"\d{1,10}", str(version_code)):
            raise JobError("The version code must be a whole number.")
        text = _edit_tags(text, ("manifest",), lambda t: _set_attr(t, "android:versionCode", str(version_code)))
        report.append(f"Version code: {version_code}")

    if label is not None and label != current.label:
        label = label.strip()
        if not label:
            raise JobError("The app name cannot be empty.")
        text, lines = _rename_label(project, text, current, label, all_locales)
        report += lines

    if text != original_text:
        project.manifest_path.write_text(text, encoding="utf-8")

    if icon_dir:
        report += replace_icon(project, icon_dir, current)

    if report:
        project.refresh_info()
        project.touch()
    return report


def _rename_label(project, text, current, label, all_locales):
    report = []
    string_names = []
    literal = False
    for value in current.label_values:
        ref = _parse_ref(value)
        if ref and ref[0] == "string":
            string_names.append(ref[1])
        elif not ref:
            literal = True
    if not current.label_values:
        # no label at all: give <application> one
        text = _edit_tags(text, ("application",), lambda t: _set_attr(t, "android:label", label))
        return text, [f"App name: {label}"]

    if literal:
        def relabel(tag_text):
            value = _get_attr(tag_text, "android:label")
            if value is not None and not value.startswith("@"):
                name = _get_attr(tag_text, "android:name") or ""
                if tag_text.startswith("<application") or name in current.launcher_names:
                    return _set_attr(tag_text, "android:label", label)
            return tag_text

        text = _edit_tags(text, ("application", "activity", "activity-alias"), relabel)

    changed_files = 0
    encoded = _escape_string(label)
    for name in string_names:
        pattern = _string_pattern(name)
        for path in _string_files(project):
            if not all_locales and path.parent.name != "values":
                continue
            try:
                content = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            new, hits = pattern.subn(lambda m: m.group(1) + encoded + m.group(3), content)
            if hits and new != content:
                path.write_text(new, encoding="utf-8")
                changed_files += 1
    report.append(f"App name: {label}" + (f" ({changed_files} string file(s) updated)" if changed_files else ""))
    return text, report


def _image_size(path):
    """(width, height) of a PNG or WebP file, or None when unknown."""
    try:
        with open(path, "rb") as fh:
            head = fh.read(32)
    except OSError:
        return None
    if head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR":
        return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        chunk = head[12:16]
        if chunk == b"VP8X":
            return int.from_bytes(head[24:27], "little") + 1, int.from_bytes(head[27:30], "little") + 1
        if chunk == b"VP8L" and head[20] == 0x2F:
            bits = int.from_bytes(head[21:25], "little")
            return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
        if chunk == b"VP8 " and head[23:26] == b"\x9d\x01\x2a":
            return (int.from_bytes(head[26:28], "little") & 0x3FFF,
                    int.from_bytes(head[28:30], "little") & 0x3FFF)
    return None


def _icon_size(path):
    size = _image_size(path)
    if size and 0 < max(size) <= 2048:
        return max(size)
    for qualifier in path.parent.name.split("-")[1:]:
        if qualifier in DENSITIES:
            return DENSITIES[qualifier]
    return 192


def _icon_plan(project, current):
    """What replacing the icon involves.

    Returns (writes, removals): writes is a list of (target_path, size,
    replaced_path_or_None); removals are adaptive/vector definitions that
    would otherwise win over the new images on modern devices.
    """
    writes, removals = [], []
    for ref in current.icon_refs:
        files = _resource_files(project, ref)
        rasters = [p for p in files if p.suffix.lower() in RASTER_EXTS]
        removals += [p for p in files if p not in rasters]
        for path in rasters:
            writes.append((path.with_name(ref[1] + ".png"), _icon_size(path), path))
        if not rasters:
            base = files[0].parent.parent if files else next(iter(_res_dirs(project)), None)
            if base is None:
                continue
            for density, size in DENSITIES.items():
                if density != "ldpi":
                    writes.append((base / f"{ref[0]}-{density}" / f"{ref[1]}.png", size, None))
    return writes, removals


def icon_sizes(project):
    """Square sizes (pixels) the UI has to render before calling apply()."""
    writes, _removals = _icon_plan(project, read(project))
    return sorted({size for _target, size, _old in writes})


def replace_icon(project, icon_dir, current=None):
    """Replace the launcher icon.

    icon_dir holds the new icon pre-rendered as '<size>.png' for every
    size returned by icon_sizes(). Returns report lines.
    """
    import shutil

    current = current or read(project)
    if not current.icon_refs:
        raise JobError("The manifest does not declare an icon to replace.")
    writes, removals = _icon_plan(project, current)
    if not writes:
        raise JobError("No icon files were found in the project's resources.")
    icon_dir = Path(icon_dir)
    for _target, size, _old in writes:
        if not (icon_dir / f"{size}.png").is_file():
            raise JobError(f"The new icon is missing its {size}px rendering.")
    for target, size, old in writes:
        if old is not None and old != target:
            old.unlink(missing_ok=True)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(icon_dir / f"{size}.png", target)
    for path in removals:
        path.unlink(missing_ok=True)
    line = f"Icon: {len(writes)} image(s) written"
    if removals:
        line += f", {len(removals)} adaptive/vector definition(s) removed"
    return [line]


def validate_package(name):
    return bool(PACKAGE_RE.fullmatch(name or ""))


def icon_preview_path(project):
    """Absolute path of an image representing the app, or None."""
    rel = project.meta.get("icon")
    if rel:
        path = Path(project.src) / rel
        if path.is_file():
            return path
    return None
