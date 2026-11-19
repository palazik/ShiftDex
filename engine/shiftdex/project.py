# SPDX-License-Identifier: GPL-3.0-or-later
"""Projects: one unpacked APK or JAR living in the workspace folder.

Layout of a project directory:

    project.json     metadata
    original/        untouched copy of the file that was opened
    src/             the unpacked tree the user edits
    java/            optional read-only Java sources made by jadx
    baseline.json    file hashes taken right after unpacking
    backups/         snapshots and single-file backups
    build/           build output before it is copied to its destination
"""

import hashlib
import os
import re
import shutil
import time
from pathlib import Path

from . import config, signing, tools
from .runner import JobError

APK_EXTS = (".apk",)
SPLIT_EXTS = (".xapk", ".apks", ".apkm")
JAR_EXTS = (".jar", ".aar", ".war")
SUPPORTED_EXTS = APK_EXTS + SPLIT_EXTS + JAR_EXTS

# How a project's code is shown. Smali is what gets edited and built; Java
# is a decompiled, read-only rendering of the same code.
CODE_VIEWS = ("smali", "java")

# APKEditor's rebuild cache. Not user content, so it is left out of change
# tracking and plugins.
IGNORED_TOP = (".cache",)


def kind_of(path):
    """'apk', 'split', 'jar' or None for an unsupported file."""
    ext = Path(path).suffix.lower()
    if ext in APK_EXTS:
        return "apk"
    if ext in SPLIT_EXTS:
        return "split"
    if ext in JAR_EXTS:
        return "jar"
    return None


def _slug(text):
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", text).strip("-.")
    return slug[:60] or "project"


def is_within(root, path):
    root = os.path.realpath(root)
    path = os.path.realpath(path)
    return path == root or path.startswith(root + os.sep)


def safe_relpath(rel):
    """Normalise a project-relative path, rejecting anything that escapes."""
    rel = str(rel).replace("\\", "/")
    if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel):
        raise JobError(f"Absolute paths are not allowed: {rel}")
    parts = [p for p in rel.split("/") if p not in ("", ".")]
    if any(p == ".." for p in parts):
        raise JobError(f"Path leaves the project: {rel}")
    return "/".join(parts)


def hash_file(path):
    digest = hashlib.blake2b(digest_size=16)
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def walk_files(root, ignore_top=IGNORED_TOP):
    """Yield project-relative posix paths of every file under root."""
    root = str(root)
    for base, dirs, files in os.walk(root):
        rel_base = os.path.relpath(base, root)
        if rel_base == ".":
            dirs[:] = [d for d in dirs if d not in ignore_top]
            rel_base = ""
        dirs.sort()
        for name in sorted(files):
            yield f"{rel_base}/{name}".replace(os.sep, "/") if rel_base else name


class Project:
    def __init__(self, directory, meta):
        self.dir = Path(directory)
        self.meta = meta

    # -- locations ---------------------------------------------------------

    @property
    def src(self):
        return self.dir / "src"

    @property
    def original_dir(self):
        return self.dir / "original"

    @property
    def original_file(self):
        name = self.meta.get("original_name")
        return self.original_dir / name if name else None

    @property
    def build_dir(self):
        return self.dir / "build"

    @property
    def java_dir(self):
        return self.dir / "java"

    @property
    def java_sources(self):
        return self.java_dir / "sources"

    @property
    def has_java(self):
        return self.java_sources.is_dir()

    @property
    def code_view(self):
        view = self.meta.get("code_view")
        return view if view in CODE_VIEWS else "smali"

    @property
    def backups_dir(self):
        return self.dir / "backups"

    @property
    def id(self):
        return self.dir.name

    @property
    def kind(self):
        return self.meta.get("kind", "apk")

    @property
    def name(self):
        return self.meta.get("name") or self.dir.name

    @property
    def is_apk(self):
        return self.kind == "apk"

    @property
    def manifest_path(self):
        return self.src / "AndroidManifest.xml"

    @property
    def can_edit_identity(self):
        return self.is_apk and self.manifest_path.is_file()

    def path(self, rel):
        """Absolute path of a project-relative file, guaranteed inside src."""
        target = self.src / safe_relpath(rel)
        if not is_within(self.src, target.parent):
            raise JobError(f"Path leaves the project: {rel}")
        return target

    def rel(self, path):
        return os.path.relpath(path, self.src).replace(os.sep, "/")

    # -- persistence -------------------------------------------------------

    @classmethod
    def load(cls, directory):
        directory = Path(directory)
        meta = config.read_json(directory / "project.json")
        if not isinstance(meta, dict) or "kind" not in meta:
            return None
        return cls(directory, meta)

    @classmethod
    def list(cls, workspace):
        workspace = Path(workspace)
        found = []
        if workspace.is_dir():
            for child in workspace.iterdir():
                if child.is_dir():
                    project = cls.load(child)
                    if project:
                        found.append(project)
        found.sort(key=lambda p: p.meta.get("modified", 0), reverse=True)
        return found

    def save(self):
        config.write_json(self.dir / "project.json", self.meta)

    def touch(self):
        self.meta["modified"] = time.time()
        self.save()

    def delete(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    # -- creation ----------------------------------------------------------

    @classmethod
    def create(cls, workspace, input_path, settings, ctx):
        """Copy input_path into a new project and unpack it."""
        input_path = Path(input_path)
        kind = kind_of(input_path)
        if kind is None:
            raise JobError(f"{input_path.name} is not an APK or JAR file.")
        if not input_path.is_file():
            raise JobError(f"{input_path} does not exist.")

        workspace = Path(workspace)
        workspace.mkdir(parents=True, exist_ok=True)
        base = _slug(input_path.stem)
        directory = workspace / base
        counter = 2
        while directory.exists():
            directory = workspace / f"{base}-{counter}"
            counter += 1
        directory.mkdir()

        now = time.time()
        project = cls(directory, {
            "format": 1,
            "kind": "jar" if kind == "jar" else "apk",
            "name": input_path.stem,
            "source_path": str(input_path),
            "original_name": input_path.name,
            "created": now,
            "modified": now,
            "applied_plugins": [],
        })
        try:
            project.original_dir.mkdir()
            ctx.log(f"Copying {input_path.name} …")
            shutil.copyfile(input_path, project.original_file)
            project.save()
            project.unpack(settings, ctx)
        except BaseException:
            shutil.rmtree(directory, ignore_errors=True)
            raise
        return project

    # -- unpack ------------------------------------------------------------

    def unpack(self, settings, ctx):
        """(Re)create src/ from the original file."""
        original = self.original_file
        if not original or not original.is_file():
            raise JobError("The original file of this project is missing.")
        staging = self.dir / "src.new"
        shutil.rmtree(staging, ignore_errors=True)
        try:
            if self.kind == "jar":
                self._unpack_jar(original, staging, settings, ctx)
            else:
                self._unpack_apk(original, staging, settings, ctx)
            ctx.check()
            shutil.rmtree(self.src, ignore_errors=True)
            os.replace(staging, self.src)
        finally:
            shutil.rmtree(staging, ignore_errors=True)
        ctx.log("Indexing files …")
        self.write_baseline(ctx)
        self.refresh_info()
        self.touch()

    def _unpack_apk(self, original, dest, settings, ctx):
        base = tools.apkeditor_command(settings)
        apk = original
        merged = None
        if kind_of(original) == "split":
            merged = self.dir / "merged.apk"
            ctx.log("Merging split APKs …")
            ctx.run(base + ["m", "-i", original, "-o", merged, "-f"])
            apk = merged
        try:
            decode_type = settings.get("decode_type")
            if decode_type not in ("xml", "json", "raw"):
                decode_type = "xml"
            cmd = base + ["d", "-t", decode_type, "-i", apk, "-o", dest, "-f"]
            if not settings.get("decode_smali"):
                cmd.append("-dex")
            ctx.run(cmd)
            self.meta["decode_type"] = decode_type
        finally:
            if merged:
                merged.unlink(missing_ok=True)

    def _unpack_jar(self, original, dest, settings, ctx):
        # jar-info.tsv remembers entry order and which entries were stored
        # uncompressed, so a rebuild can keep both as they were
        ctx.run(tools.jartool_command(settings) + ["unpack", original, dest, self.dir / "jar-info.tsv"])

    # -- java view ---------------------------------------------------------

    def decompile_java(self, settings, ctx, current=False):
        """(Re)create java/ with jadx.

        Decompiles the original file, or with current=True the project as
        it is now, so the Java shows the user's smali edits.
        """
        command = tools.jadx_command(settings)
        original = self.original_file
        packed = None
        if current or not original or not original.is_file() or kind_of(original) == "split":
            # pack what is on disk; only ever used as input for the decompiler
            self.build_dir.mkdir(exist_ok=True)
            packed = self.build_dir / ("java-input.jar" if self.kind == "jar" else "java-input.apk")
            packed.unlink(missing_ok=True)
            ctx.log("Packing the current project for the decompiler …")
            if self.kind == "jar":
                self._build_jar(packed, settings, ctx)
            else:
                ctx.run(tools.apkeditor_command(settings) + ["b", "-i", self.src, "-o", packed, "-f"])
            source, origin = packed, "current"
        else:
            source, origin = original, "original"

        staging = self.dir / "java.new"
        shutil.rmtree(staging, ignore_errors=True)
        threads = max(1, min(os.cpu_count() or 2, 8))
        try:
            ctx.log("Decompiling to Java with jadx. Large apps take a few minutes …")
            try:
                # --rename-flags none keeps jadx from renaming classes, so every
                # Java file can be matched with its smali file by path
                ctx.run(command + ["-d", staging, "--no-res", "--rename-flags", "none",
                                   "--threads-count", threads, source], on_line=_jadx_progress(ctx))
            except JobError:
                # jadx exits non-zero when single methods fail to decompile;
                # what it did produce is still perfectly usable
                if not (staging / "sources").is_dir():
                    raise
                ctx.log("jadx could not decompile some methods; they are marked in the source.")
            ctx.check()
            sources = staging / "sources"
            count = sum(1 for _ in sources.rglob("*.java")) if sources.is_dir() else 0
            if not count:
                raise JobError("jadx produced no Java files for this project.")
            shutil.rmtree(self.java_dir, ignore_errors=True)
            os.replace(staging, self.java_dir)
        finally:
            shutil.rmtree(staging, ignore_errors=True)
            if packed:
                packed.unlink(missing_ok=True)
        tool = tools.jadx()
        self.meta["java"] = {"time": time.time(), "from": origin, "files": count,
                             "jadx": tool["version"] if tool else ""}
        self.save()
        ctx.log(f"Java view ready: {count} files.")
        return self.meta["java"]

    def set_code_view(self, view):
        if view not in CODE_VIEWS:
            raise JobError("The code view is either 'smali' or 'java'.")
        self.meta["code_view"] = view
        self.save()

    # -- metadata ----------------------------------------------------------

    def refresh_info(self):
        """Re-read package, version and label from the unpacked tree."""
        info = {}
        if self.kind == "jar":
            info = _read_jar_manifest(self.src / "META-INF" / "MANIFEST.MF")
        elif self.manifest_path.is_file():
            from . import identity

            try:
                current = identity.read(self)
                info = {
                    "package": current.package,
                    "label": current.label,
                    "version_name": current.version_name,
                    "version_code": current.version_code,
                    "icon": current.icon_file or "",
                }
            except JobError:
                info = {}
        for key in ("package", "label", "version_name", "version_code", "icon", "main_class"):
            if key in info:
                self.meta[key] = info[key]
        return info

    # -- change tracking ---------------------------------------------------

    def write_baseline(self, ctx=None):
        index = {}
        for rel in walk_files(self.src):
            if ctx:
                ctx.check()
            path = self.src / rel
            try:
                index[rel] = [path.stat().st_size, hash_file(path)]
            except OSError:
                continue
        config.write_json(self.dir / "baseline.json", index)
        self.meta["file_count"] = len(index)

    def changes(self, ctx=None):
        """Compare src/ with the baseline taken at unpack time.

        Returns {'added': [...], 'modified': [...], 'deleted': [...]}.
        """
        baseline = config.read_json(self.dir / "baseline.json", {}) or {}
        added, modified = [], []
        seen = set()
        for rel in walk_files(self.src):
            if ctx:
                ctx.check()
            seen.add(rel)
            known = baseline.get(rel)
            path = self.src / rel
            if known is None:
                added.append(rel)
                continue
            try:
                if path.stat().st_size != known[0] or hash_file(path) != known[1]:
                    modified.append(rel)
            except OSError:
                continue
        deleted = sorted(set(baseline) - seen)
        return {"added": added, "modified": modified, "deleted": deleted}

    # -- build -------------------------------------------------------------

    def default_output_name(self):
        original = Path(self.meta.get("original_name", self.name))
        ext = ".jar" if self.kind == "jar" else ".apk"
        if self.kind == "jar" and original.suffix.lower() in JAR_EXTS:
            ext = original.suffix.lower()
        return f"{original.stem}_mod{ext}"

    def build(self, settings, ctx, dest):
        """Pack src/ and write the result to dest. Returns a result dict."""
        dest = Path(dest)
        if is_within(self.src, dest):
            raise JobError("Choose an output location outside the project's source folder.")
        self.build_dir.mkdir(exist_ok=True)
        staged = self.build_dir / ("out" + dest.suffix)
        staged.unlink(missing_ok=True)
        signed = False
        if self.kind == "jar":
            self._build_jar(staged, settings, ctx)
        else:
            ctx.run(tools.apkeditor_command(settings) + ["b", "-i", self.src, "-o", staged, "-f"])
            if not staged.is_file():
                raise JobError("APKEditor finished without producing an APK.")
            if settings.get("sign_after_build"):
                signed = signing.sign(staged, settings, ctx)
        ctx.check()
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(staged, dest)
        staged.unlink(missing_ok=True)
        self.meta["last_build"] = {"path": str(dest), "time": time.time(), "signed": signed}
        self.touch()
        ctx.log(f"Saved to {dest}")
        return {"path": str(dest), "signed": signed, "size": dest.stat().st_size}

    def _build_jar(self, out, settings, ctx):
        cmd = tools.jartool_command(settings) + ["pack", self.src, out, self.dir / "jar-info.tsv"]
        if settings.get("jar_strip_signatures"):
            cmd.append("--strip-signatures")
        ctx.run(cmd)


def _jadx_progress(ctx):
    """Turns jadx's 'progress: 120 of 4000 (3%)' ticker into a progress bar."""
    pattern = re.compile(r"progress: (\d+) of (\d+)")

    def on_line(line):
        match = pattern.search(line)
        if not match:
            return not line.strip()
        total = int(match.group(2))
        if total:
            ctx.progress(int(match.group(1)) / total)
        return True

    return on_line


def _read_jar_manifest(path):
    info = {}
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return info
    # continuation lines start with a single space
    text = re.sub(r"\r?\n ", "", text)
    fields = {}
    for line in text.splitlines():
        if not line.strip():
            break  # main section ends at the first blank line
        key, sep, value = line.partition(":")
        if sep:
            fields[key.strip().lower()] = value.strip()
    info["main_class"] = fields.get("main-class", "")
    info["label"] = fields.get("implementation-title") or fields.get("bundle-name") or ""
    info["version_name"] = fields.get("implementation-version") or fields.get("bundle-version") or ""
    info["package"] = fields.get("automatic-module-name") or fields.get("bundle-symbolicname") or ""
    return info
