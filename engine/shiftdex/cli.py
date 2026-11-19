# SPDX-License-Identifier: GPL-3.0-or-later
"""Command line front end of the engine.

The desktop app drives the engine through this module, and it works just
as well from a terminal or a script:

    python3 -m shiftdex.cli create app.apk
    python3 -m shiftdex.cli build ~/.local/share/shiftdex/projects/app out.apk

With --json every line on stdout is one JSON object:

    {"log": "text"}                 progress output
    {"progress": 0.42}              download progress, 0..1
    {"ok": true, "result": ...}     last line on success
    {"ok": false, "error": "..."}   last line on failure
"""

import argparse
import json
import signal
import sys
from pathlib import Path

from . import __version__, backup, config, identity, plugins, signing, tools
from .project import Project
from .runner import Cancelled, JobContext, JobError


class Output:
    def __init__(self, as_json):
        self.as_json = as_json

    def _emit(self, payload):
        sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
        sys.stdout.flush()

    def log(self, text):
        if self.as_json:
            for line in str(text).splitlines() or [""]:
                self._emit({"log": line})
        else:
            print(text, flush=True)

    def progress(self, fraction):
        if self.as_json:
            self._emit({"progress": round(fraction, 3)})

    def result(self, value):
        if self.as_json:
            self._emit({"ok": True, "result": value})
        elif value not in (None, "", [], {}):
            print(json.dumps(value, indent=2, ensure_ascii=False))

    def error(self, message):
        if self.as_json:
            self._emit({"ok": False, "error": message})
        else:
            print(f"error: {message}", file=sys.stderr)


def describe(project):
    """Everything the UI needs to know about a project."""
    data = dict(project.meta)
    icon = identity.icon_preview_path(project)
    original = project.original_file
    data.update({
        "id": project.id,
        "dir": str(project.dir),
        "src": str(project.src),
        "kind": project.kind,
        "name": project.name,
        "icon_path": str(icon) if icon else "",
        "can_edit_identity": project.can_edit_identity,
        "original_file": str(original) if original and original.is_file() else "",
        "default_output": project.default_output_name(),
        "code_view": project.code_view,
        "java_dir": str(project.java_sources) if project.has_java else "",
        "has_smali": (project.src / "smali").is_dir(),
    })
    return data


def _project(path):
    project = Project.load(path)
    if project is None:
        raise JobError(f"{path} is not a ShiftDex project.")
    return project


def _plugin(plugin_id):
    installed, _broken = plugins.list_installed()
    for plugin in installed:
        if plugin.id == plugin_id:
            return plugin
    raise JobError(f"Plugin {plugin_id} is not installed.")


def _plugin_info(plugin, project=None):
    return {
        "id": plugin.id,
        "name": plugin.name,
        "version": plugin.version,
        "author": plugin.author,
        "description": plugin.description,
        "target": plugin.target,
        "code": plugin.code,
        "code_label": plugin.code_label,
        "summary": plugin.summary(),
        "dir": str(plugin.dir),
        "problems": plugins.compatibility(plugin, project) if project else [],
    }


def _read_spec(path):
    spec = config.read_json(path)
    if not isinstance(spec, dict):
        raise JobError("The request file could not be read.")
    return spec


# -- commands ------------------------------------------------------------------

def cmd_status(args, settings, ctx, out):
    java = tools.find_java(settings)
    version = tools.java_version(java)
    editor = tools.apkeditor()
    return {
        "version": __version__,
        "java": java or "",
        "java_version": version or "",
        "java_ok": tools.java_major(version) >= 8,
        "jdk": bool(tools.find_java_tool("javap", settings)),
        "apkeditor": editor["version"] if editor else "",
        "jadx": (tools.jadx() or {}).get("version", ""),
        "signer": tools.signer_summary() or "",
        "apksigner": bool(tools.find_apksigner()),
        "jartool": str(tools.jartool() or ""),
        "workspace": str(settings.workspace),
        "plugins_dir": str(config.plugins_dir()),
        "keystore": str(signing.auto_keystore_path()),
    }


def cmd_install_apkeditor(args, settings, ctx, out):
    return {"version": tools.install_apkeditor(ctx, out.progress)}


def cmd_install_signer(args, settings, ctx, out):
    return {"version": tools.install_signer(ctx, out.progress)}


def cmd_install_jadx(args, settings, ctx, out):
    return {"version": tools.install_jadx(ctx, out.progress)}


def cmd_java_decompile(args, settings, ctx, out):
    project = _project(args.project)
    project.decompile_java(settings, ctx, current=args.current)
    return describe(project)


def cmd_code_view(args, settings, ctx, out):
    project = _project(args.project)
    project.set_code_view(args.view)
    return describe(project)


def cmd_use_apkeditor(args, settings, ctx, out):
    return {"version": tools.install_local_apkeditor(args.jar)}


def cmd_projects(args, settings, ctx, out):
    return [describe(p) for p in Project.list(settings.workspace)]


def cmd_create(args, settings, ctx, out):
    return describe(Project.create(settings.workspace, args.file, settings, ctx))


def cmd_info(args, settings, ctx, out):
    project = _project(args.project)
    project.refresh_info()
    project.save()
    return describe(project)


def cmd_touch(args, settings, ctx, out):
    project = _project(args.project)
    project.refresh_info()
    project.touch()
    return describe(project)


def cmd_delete(args, settings, ctx, out):
    _project(args.project).delete()


def cmd_reset(args, settings, ctx, out):
    project = _project(args.project)
    if settings.get("snapshot_before_restore") and project.src.is_dir():
        backup.create_snapshot(project, ctx, "Before resetting to the original", "auto")
    project.unpack(settings, ctx)
    return describe(project)


def cmd_build(args, settings, ctx, out):
    project = _project(args.project)
    result = project.build(settings, ctx, args.dest)
    result["project"] = describe(project)
    return result


def cmd_changes(args, settings, ctx, out):
    return _project(args.project).changes(ctx)


def cmd_snapshot(args, settings, ctx, out):
    return backup.create_snapshot(_project(args.project), ctx, args.note or "", "manual")


def cmd_snapshots(args, settings, ctx, out):
    return backup.list_snapshots(_project(args.project))


def cmd_snapshot_restore(args, settings, ctx, out):
    project = _project(args.project)
    if settings.get("snapshot_before_restore"):
        backup.create_snapshot(project, ctx, "Before restoring a snapshot", "auto")
    backup.restore_snapshot(project, args.id, ctx)
    return describe(project)


def cmd_snapshot_delete(args, settings, ctx, out):
    backup.delete_snapshot(_project(args.project), args.id)


def cmd_snapshot_export(args, settings, ctx, out):
    backup.export_snapshot(_project(args.project), args.id, args.dest)


def cmd_file_backup(args, settings, ctx, out):
    return backup.backup_file(_project(args.project), args.rel)


def cmd_file_backups(args, settings, ctx, out):
    return backup.list_file_backups(_project(args.project), args.rel)


def cmd_file_restore(args, settings, ctx, out):
    backup.restore_file(_project(args.project), args.rel, args.id)


def cmd_file_backup_delete(args, settings, ctx, out):
    backup.delete_file_backup(_project(args.project), args.rel, args.id)


def cmd_plugins(args, settings, ctx, out):
    project = _project(args.project) if args.project else None
    installed, broken = plugins.list_installed()
    return {
        "plugins": [_plugin_info(p, project) for p in installed],
        "broken": [{"folder": folder, "error": reason} for folder, reason in broken],
        "applied": project.meta.get("applied_plugins", []) if project else [],
    }


def cmd_plugin_install(args, settings, ctx, out):
    return _plugin_info(plugins.install_file(args.file))


def cmd_plugin_remove(args, settings, ctx, out):
    plugins.remove(_plugin(args.id))


def cmd_plugin_export(args, settings, ctx, out):
    plugins.export(_plugin(args.id), args.dest)


def cmd_plugin_apply(args, settings, ctx, out):
    project = _project(args.project)
    plugin = _plugin(args.id)
    problems = plugins.compatibility(plugin, project)
    if problems and not args.force:
        raise JobError("\n".join(problems))
    # check first, so a plugin that does not fit costs no snapshot
    problems = plugins.precheck(plugin, project)
    if problems:
        raise JobError("The plugin does not fit this project:\n" + "\n".join(problems))
    if settings.get("snapshot_before_plugin"):
        backup.create_snapshot(project, ctx, f"Before plugin: {plugin.name}", "auto")
    ctx.log(f"Applying {plugin.name} ({plugin.code_label} plugin) …")
    report = plugins.apply(plugin, project, ctx)
    return {"report": report, "code": plugin.code, "project": describe(project)}


def cmd_plugin_create(args, settings, ctx, out):
    spec = _read_spec(args.spec)
    project = _project(args.project) if args.project else None
    plugin = plugins.create(
        spec.get("info", {}),
        project,
        files=spec.get("files", []),
        deleted=spec.get("deleted", []),
        rules=spec.get("rules", []),
        restrict_package=bool(spec.get("restrict_package")),
        overwrite=bool(spec.get("overwrite")),
    )
    return _plugin_info(plugin)


def cmd_identity(args, settings, ctx, out):
    project = _project(args.project)
    current = identity.read(project)
    icon = project.src / current.icon_file if current.icon_file else None
    return {
        "package": current.package,
        "label": current.label,
        "version_name": current.version_name,
        "version_code": current.version_code,
        "icon_path": str(icon) if icon else "",
        "icon_sizes": identity.icon_sizes(project),
        "has_icon": bool(current.icon_refs),
    }


def cmd_identity_apply(args, settings, ctx, out):
    project = _project(args.project)
    spec = _read_spec(args.spec)
    report = identity.apply(
        project,
        label=spec.get("label"),
        package=spec.get("package"),
        icon_dir=spec.get("icon_dir"),
        version_name=spec.get("version_name"),
        version_code=spec.get("version_code"),
        all_locales=bool(spec.get("all_locales", True)),
        rename_refs=bool(spec.get("rename_refs", True)),
    )
    project.save()
    return {"report": report, "project": describe(project)}


def cmd_disasm(args, settings, ctx, out):
    text = ctx.run(tools.jartool_command(settings) + ["disasm", args.file], quiet=True)
    return {"text": text}


def build_parser():
    parser = argparse.ArgumentParser(prog="shiftdex-engine", description="ShiftDex engine")
    parser.add_argument("--json", action="store_true", help="machine readable output")
    parser.add_argument("--version", action="version", version=f"ShiftDex {__version__}")
    sub = parser.add_subparsers(dest="command", required=True, metavar="command")

    def add(name, func, *arguments, help_text=""):
        command = sub.add_parser(name, help=help_text)
        for argument in arguments:
            if isinstance(argument, tuple):
                command.add_argument(argument[0], **argument[1])
            else:
                command.add_argument(argument)
        command.set_defaults(func=func)

    optional = lambda flag: (flag, {"default": None})  # noqa: E731
    add("status", cmd_status, help_text="show which tools are available")
    add("install-apkeditor", cmd_install_apkeditor, help_text="download the latest APKEditor")
    add("install-signer", cmd_install_signer, help_text="download uber-apk-signer")
    add("install-jadx", cmd_install_jadx, help_text="download jadx for the Java view")
    add("use-apkeditor", cmd_use_apkeditor, "jar", help_text="use a local APKEditor jar")
    add("java-decompile", cmd_java_decompile, "project", ("--current", {"action": "store_true"}),
        help_text="create the read-only Java view of a project")
    add("code-view", cmd_code_view, "project", ("view", {"choices": ["smali", "java"]}),
        help_text="set whether a project shows smali or Java")
    add("projects", cmd_projects, help_text="list projects")
    add("create", cmd_create, "file", help_text="unpack an APK or JAR into a new project")
    add("info", cmd_info, "project", help_text="show a project")
    add("touch", cmd_touch, "project")
    add("delete", cmd_delete, "project", help_text="delete a project")
    add("reset", cmd_reset, "project", help_text="unpack the original file again")
    add("build", cmd_build, "project", "dest", help_text="pack a project")
    add("changes", cmd_changes, "project", help_text="list files changed since unpacking")
    add("snapshot", cmd_snapshot, "project", optional("--note"), help_text="back up the whole project")
    add("snapshots", cmd_snapshots, "project")
    add("snapshot-restore", cmd_snapshot_restore, "project", "id")
    add("snapshot-delete", cmd_snapshot_delete, "project", "id")
    add("snapshot-export", cmd_snapshot_export, "project", "id", "dest")
    add("file-backup", cmd_file_backup, "project", "rel", help_text="back up one file")
    add("file-backups", cmd_file_backups, "project", optional("--rel"))
    add("file-restore", cmd_file_restore, "project", "rel", "id")
    add("file-backup-delete", cmd_file_backup_delete, "project", "rel", "id")
    add("plugins", cmd_plugins, optional("--project"), help_text="list installed plugins")
    add("plugin-install", cmd_plugin_install, "file")
    add("plugin-remove", cmd_plugin_remove, "id")
    add("plugin-export", cmd_plugin_export, "id", "dest")
    add("plugin-apply", cmd_plugin_apply, "project", "id", ("--force", {"action": "store_true"}),
        help_text="apply a plugin to a project")
    add("plugin-create", cmd_plugin_create, optional("--project"), ("--spec", {"required": True}))
    add("identity", cmd_identity, "project", help_text="show app name, package and version")
    add("identity-apply", cmd_identity_apply, "project", ("--spec", {"required": True}))
    add("disasm", cmd_disasm, "file", help_text="disassemble a .class file")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    out = Output(args.json)
    ctx = JobContext(out.log)
    ctx.progress = out.progress
    signal.signal(signal.SIGTERM, lambda _sig, _frame: ctx.cancel())
    signal.signal(signal.SIGINT, lambda _sig, _frame: ctx.cancel())
    settings = config.Settings()
    try:
        result = args.func(args, settings, ctx, out)
        ctx.check()
    except Cancelled:
        out.error("Cancelled")
        return 130
    except JobError as exc:
        out.error(str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001 - last line of defence, reported to the caller
        import traceback

        out.log(traceback.format_exc())
        out.error(f"{type(exc).__name__}: {exc}")
        return 1
    out.result(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
