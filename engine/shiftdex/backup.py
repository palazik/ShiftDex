# SPDX-License-Identifier: GPL-3.0-or-later
"""Backups: whole-project snapshots and copies of single files."""

import os
import re
import shutil
import tarfile
import time
from pathlib import Path

from . import config
from .project import safe_relpath
from .runner import JobError

_STAMP = "%Y%m%d-%H%M%S"
_HAS_TAR_FILTERS = hasattr(tarfile, "data_filter")


def _stamp():
    return time.strftime(_STAMP)


def _unique(directory, stem, suffix=""):
    candidate = directory / f"{stem}{suffix}"
    counter = 2
    while candidate.exists():
        candidate = directory / f"{stem}-{counter}{suffix}"
        counter += 1
    return candidate


# -- snapshots ---------------------------------------------------------------

def snapshots_dir(project):
    return project.backups_dir / "snapshots"


def create_snapshot(project, ctx, note="", kind="manual"):
    """Archive the whole src/ tree. Returns the snapshot record."""
    if not project.src.is_dir():
        raise JobError("There is nothing to back up yet.")
    directory = snapshots_dir(project)
    directory.mkdir(parents=True, exist_ok=True)
    archive = _unique(directory, _stamp(), ".tar.gz")
    tmp = archive.with_name(archive.name + ".part")
    count = 0
    ctx.log("Creating snapshot …")
    try:
        # level 1: smali and xml compress well even at the fastest setting
        with tarfile.open(tmp, "w:gz", compresslevel=1) as tar:
            for base, dirs, files in os.walk(project.src):
                dirs.sort()
                for name in sorted(files):
                    ctx.check()
                    full = os.path.join(base, name)
                    tar.add(full, arcname=os.path.relpath(full, project.src), recursive=False)
                    count += 1
        os.replace(tmp, archive)
    finally:
        tmp.unlink(missing_ok=True)
    record = {
        "id": archive.name[: -len(".tar.gz")],
        "note": note.strip(),
        "kind": kind,
        "time": time.time(),
        "files": count,
        "size": archive.stat().st_size,
    }
    config.write_json(_meta_path(archive), record)
    ctx.log(f"Snapshot saved: {count} files, {format_size(record['size'])}")
    return record


def _meta_path(archive):
    return archive.with_name(archive.name[: -len(".tar.gz")] + ".json")


def list_snapshots(project):
    directory = snapshots_dir(project)
    records = []
    if directory.is_dir():
        for archive in directory.glob("*.tar.gz"):
            record = config.read_json(_meta_path(archive), None)
            if not isinstance(record, dict):
                stat = archive.stat()
                record = {"note": "", "kind": "manual", "time": stat.st_mtime, "files": 0, "size": stat.st_size}
            record["id"] = archive.name[: -len(".tar.gz")]
            records.append(record)
    records.sort(key=lambda r: r.get("time", 0), reverse=True)
    return records


def _archive(project, snapshot_id):
    if not re.fullmatch(r"[0-9A-Za-z._-]+", snapshot_id or ""):
        raise JobError("Invalid snapshot name.")
    archive = snapshots_dir(project) / f"{snapshot_id}.tar.gz"
    if not archive.is_file():
        raise JobError("That snapshot no longer exists.")
    return archive


def restore_snapshot(project, snapshot_id, ctx):
    """Replace src/ with the contents of a snapshot."""
    archive = _archive(project, snapshot_id)
    staging = project.dir / "src.restore"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    ctx.log("Restoring snapshot …")
    try:
        with tarfile.open(archive, "r:gz") as tar:
            for member in tar:
                ctx.check()
                # only plain files and directories, and never outside staging
                if not (member.isfile() or member.isdir()):
                    continue
                rel = safe_relpath(member.name)
                if not rel:
                    continue
                member.name = rel
                if _HAS_TAR_FILTERS:
                    tar.extract(member, staging, filter="data")
                else:
                    # Python before 3.11.4 has no extraction filters; the
                    # checks above already limit members to safe plain files
                    member.mode = (member.mode or 0o644) & 0o755
                    tar.extract(member, staging)
        old = project.dir / "src.old"
        shutil.rmtree(old, ignore_errors=True)
        if project.src.exists():
            os.replace(project.src, old)
        os.replace(staging, project.src)
        shutil.rmtree(old, ignore_errors=True)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    project.refresh_info()
    project.touch()
    ctx.log("Snapshot restored.")


def delete_snapshot(project, snapshot_id):
    archive = _archive(project, snapshot_id)
    archive.unlink(missing_ok=True)
    _meta_path(archive).unlink(missing_ok=True)


def export_snapshot(project, snapshot_id, dest):
    shutil.copyfile(_archive(project, snapshot_id), dest)


# -- single files ------------------------------------------------------------

def files_dir(project):
    return project.backups_dir / "files"


def backup_file(project, rel):
    """Keep a copy of one file. Returns the backup record."""
    rel = safe_relpath(rel)
    source = project.path(rel)
    if not source.is_file():
        raise JobError(f"{rel} is not a file.")
    directory = files_dir(project) / rel
    directory.mkdir(parents=True, exist_ok=True)
    target = _unique(directory, _stamp())
    shutil.copyfile(source, target)
    return {"rel": rel, "id": target.name, "time": target.stat().st_mtime, "size": target.stat().st_size}


def list_file_backups(project, rel=None):
    """Backups of one file, or of every file when rel is None. Newest first."""
    root = files_dir(project)
    records = []
    if rel is not None:
        directory = root / safe_relpath(rel)
        candidates = [(safe_relpath(rel), p) for p in directory.iterdir()] if directory.is_dir() else []
    else:
        candidates = []
        if root.is_dir():
            for base, _dirs, files in os.walk(root):
                for name in files:
                    full = Path(base) / name
                    candidates.append((os.path.relpath(base, root).replace(os.sep, "/"), full))
    for file_rel, path in candidates:
        if not path.is_file():
            continue
        stat = path.stat()
        records.append({"rel": file_rel, "id": path.name, "time": stat.st_mtime, "size": stat.st_size})
    records.sort(key=lambda r: r["time"], reverse=True)
    return records


def _file_backup(project, rel, backup_id):
    if not re.fullmatch(r"[0-9A-Za-z._-]+", backup_id or ""):
        raise JobError("Invalid backup name.")
    path = files_dir(project) / safe_relpath(rel) / backup_id
    if not path.is_file():
        raise JobError("That backup no longer exists.")
    return path


def restore_file(project, rel, backup_id):
    source = _file_backup(project, rel, backup_id)
    target = project.path(rel)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    project.touch()
    return target


def delete_file_backup(project, rel, backup_id):
    path = _file_backup(project, rel, backup_id)
    path.unlink(missing_ok=True)
    # drop directories left empty, up to the backups root
    parent = path.parent
    root = files_dir(project)
    while parent != root and parent.is_dir() and not any(parent.iterdir()):
        parent.rmdir()
        parent = parent.parent


# -- helpers -----------------------------------------------------------------

def format_size(size):
    size = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{int(size)} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} GB"


def format_time(timestamp):
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(timestamp))
