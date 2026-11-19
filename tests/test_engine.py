# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the engine. Run with:  python3 -m unittest discover -s tests

They build small fake projects on disk, so they need neither APKEditor nor
a real APK. The JAR round trip needs Java and a built shiftdex-jartool.jar
and is skipped without them.
"""

import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "engine"))

from shiftdex import backup, config, identity, plugins, tools  # noqa: E402
from shiftdex.project import Project, kind_of, safe_relpath  # noqa: E402
from shiftdex.runner import JobContext, JobError  # noqa: E402

MANIFEST = """<?xml version='1.0' encoding='utf-8' ?>
<manifest android:versionCode="7"
          android:versionName="1.2"
          package="com.example.app" xmlns:android="http://schemas.android.com/apk/res/android">
  <uses-permission android:name="android.permission.INTERNET" />
  <uses-permission android:name="com.example.app.permission.SYNC" />
  <permission android:name="com.example.app.permission.SYNC"
              android:protectionLevel="signature" />
  <application android:label="@string/app_name"
               android:icon="@mipmap/ic_launcher"
               android:roundIcon="@mipmap/ic_launcher_round"
               android:name=".App">
    <activity android:name="com.example.app.MainActivity"
              android:exported="true">
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
    <activity android:name="Settings" />
    <provider android:name="androidx.core.content.FileProvider"
              android:authorities="com.example.app.files;com.example.app.other"
              android:readPermission="com.example.app.permission.SYNC" />
    <meta-data android:name="com.example.app.KEY" android:value="com.example.app" />
  </application>
</manifest>
"""


def png(width, height):
    """A minimal valid PNG of the given size."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + b"\x00\x00\x00\x00" * width for _ in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class EngineCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self._env = {k: os.environ.get(k) for k in ("XDG_DATA_HOME", "XDG_CONFIG_HOME")}
        os.environ["XDG_DATA_HOME"] = str(self.tmp / "data")
        os.environ["XDG_CONFIG_HOME"] = str(self.tmp / "config")
        self.ctx = JobContext()
        self.settings = config.Settings()

    def tearDown(self):
        for key, value in self._env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        self._tmp.cleanup()

    def make_project(self, kind="apk", files=None):
        directory = self.tmp / "ws" / f"proj-{len(list((self.tmp / 'ws').glob('*'))) if (self.tmp / 'ws').exists() else 0}"
        (directory / "src").mkdir(parents=True)
        project = Project(directory, {"format": 1, "kind": kind, "name": "proj", "applied_plugins": []})
        for rel, content in (files or {}).items():
            path = project.src / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(content, bytes):
                path.write_bytes(content)
            else:
                path.write_text(content, encoding="utf-8")
        project.write_baseline()
        project.refresh_info()
        project.save()
        return project

    def make_apk_project(self):
        res = "resources/package_1/res"
        return self.make_project("apk", {
            "AndroidManifest.xml": MANIFEST,
            "resources/package_1/package.json": json.dumps({"package_id": 127, "package_name": "com.example.app"}),
            f"{res}/values/strings.xml": '<resources>\n  <string name="app_name">Example</string>\n'
                                         '  <string name="other">Example</string>\n</resources>\n',
            f"{res}/values-de/strings.xml": '<resources>\n  <string name="app_name">Beispiel</string>\n</resources>\n',
            f"{res}/mipmap-mdpi/ic_launcher.png": png(48, 48),
            f"{res}/mipmap-xhdpi/ic_launcher.png": png(96, 96),
            f"{res}/mipmap-anydpi-v26/ic_launcher.xml": "<adaptive-icon/>",
            f"{res}/mipmap-anydpi-v26/ic_launcher_round.xml": "<adaptive-icon/>",
            "smali/classes/com/example/app/MainActivity.smali": '.class public Lcom/example/app/MainActivity;\n'
                                                                '    const-string v0, "https://ads.example.com"\n',
        })


class PathTests(unittest.TestCase):
    def test_safe_relpath(self):
        self.assertEqual(safe_relpath("a/./b//c"), "a/b/c")
        self.assertEqual(safe_relpath("a\\b"), "a/b")
        for bad in ("../x", "a/../../x", "/etc/passwd", "C:\\x"):
            with self.assertRaises(JobError):
                safe_relpath(bad)

    def test_kind_of(self):
        self.assertEqual(kind_of("a.APK"), "apk")
        self.assertEqual(kind_of("a.xapk"), "split")
        self.assertEqual(kind_of("lib.aar"), "jar")
        self.assertIsNone(kind_of("notes.txt"))

    def test_java_major(self):
        self.assertEqual(tools.java_major("1.8.0_392"), 8)
        self.assertEqual(tools.java_major("21.0.4"), 21)
        self.assertEqual(tools.java_major("26-ea"), 26)
        self.assertEqual(tools.java_major(None), 0)


class SettingsTests(EngineCase):
    def test_defaults_and_bad_values(self):
        self.assertEqual(self.settings.get("theme"), "system")
        config.write_json(self.settings.path, {"theme": "neon", "editor_font_size": "big", "decode_smali": False})
        loaded = config.Settings()
        self.assertEqual(loaded.get("theme"), "system")       # unknown theme falls back
        self.assertEqual(loaded.get("editor_font_size"), 11)  # wrong type is ignored
        self.assertFalse(loaded.get("decode_smali"))

    def test_workspace(self):
        self.assertEqual(self.settings.workspace, self.tmp / "data" / "shiftdex" / "projects")
        self.settings.set("workspace_dir", str(self.tmp / "elsewhere"))
        self.assertEqual(config.Settings().workspace, self.tmp / "elsewhere")


class ChangeTests(EngineCase):
    def test_changes(self):
        project = self.make_project(files={"a.txt": "one", "b.txt": "two", ".cache/classes.dex": "x"})
        self.assertEqual(project.changes(), {"added": [], "modified": [], "deleted": []})
        (project.src / "a.txt").write_text("ONE")       # same size, different content
        (project.src / "b.txt").unlink()
        (project.src / "c" / "d.txt").parent.mkdir()
        (project.src / "c" / "d.txt").write_text("new")
        (project.src / ".cache" / "classes.dex").write_text("rebuilt")  # tool cache is not user content
        self.assertEqual(project.changes(), {"added": ["c/d.txt"], "modified": ["a.txt"], "deleted": ["b.txt"]})

    def test_path_guard(self):
        project = self.make_project(files={"a.txt": "one"})
        self.assertEqual(project.path("x/y.txt"), project.src / "x" / "y.txt")
        with self.assertRaises(JobError):
            project.path("../project.json")
        os.symlink(self.tmp, project.src / "escape")
        with self.assertRaises(JobError):
            project.path("escape/inner/file.txt")


class CodeViewTests(EngineCase):
    def test_default_and_switch(self):
        project = self.make_apk_project()
        self.assertEqual(project.code_view, "smali")  # what a fresh project starts with
        self.assertFalse(project.has_java)
        project.set_code_view("java")
        self.assertEqual(Project.load(project.dir).code_view, "java")
        project.set_code_view("smali")
        self.assertEqual(Project.load(project.dir).code_view, "smali")
        with self.assertRaises(JobError):
            project.set_code_view("kotlin")
        project.meta["code_view"] = "garbage"
        self.assertEqual(project.code_view, "smali")

    def test_java_needs_jadx(self):
        project = self.make_apk_project()
        with self.assertRaises(JobError):
            project.decompile_java(self.settings, self.ctx)  # no jadx in this empty profile
        self.assertFalse(project.has_java)

    def test_jadx_progress_lines(self):
        from shiftdex.project import _jadx_progress
        seen = []
        self.ctx.progress = seen.append
        on_line = _jadx_progress(self.ctx)
        self.assertTrue(on_line("INFO  - progress: 50 of 200 (25%)"))  # swallowed, shown as a bar
        self.assertFalse(on_line("ERROR - finished with errors, count: 3"))  # real output is kept
        self.assertTrue(on_line("   "))
        self.assertEqual(seen, [0.25])


class BackupTests(EngineCase):
    def test_snapshot_round_trip(self):
        project = self.make_project(files={"a.txt": "one", "dir/b.txt": "two"})
        snapshot = backup.create_snapshot(project, self.ctx, "first")
        self.assertEqual(snapshot["files"], 2)
        (project.src / "a.txt").write_text("changed")
        (project.src / "dir" / "b.txt").unlink()
        (project.src / "extra.txt").write_text("extra")
        backup.restore_snapshot(project, snapshot["id"], self.ctx)
        self.assertEqual((project.src / "a.txt").read_text(), "one")
        self.assertEqual((project.src / "dir" / "b.txt").read_text(), "two")
        self.assertFalse((project.src / "extra.txt").exists())
        listed = backup.list_snapshots(project)
        self.assertEqual([s["note"] for s in listed], ["first"])
        backup.delete_snapshot(project, snapshot["id"])
        self.assertEqual(backup.list_snapshots(project), [])
        with self.assertRaises(JobError):
            backup.restore_snapshot(project, "../../etc", self.ctx)

    def test_file_backups(self):
        project = self.make_project(files={"dir/a.txt": "v1"})
        first = backup.backup_file(project, "dir/a.txt")
        (project.src / "dir" / "a.txt").write_text("v2")
        second = backup.backup_file(project, "dir/a.txt")
        self.assertNotEqual(first["id"], second["id"])  # same second, still two backups
        self.assertEqual(len(backup.list_file_backups(project)), 2)
        self.assertEqual(len(backup.list_file_backups(project, "dir/a.txt")), 2)
        backup.restore_file(project, "dir/a.txt", first["id"])
        self.assertEqual((project.src / "dir" / "a.txt").read_text(), "v1")
        backup.delete_file_backup(project, "dir/a.txt", first["id"])
        backup.delete_file_backup(project, "dir/a.txt", second["id"])
        self.assertEqual(backup.list_file_backups(project), [])
        self.assertFalse((backup.files_dir(project) / "dir").exists())  # empty folders are tidied up
        with self.assertRaises(JobError):
            backup.backup_file(project, "../project.json")


class PluginTests(EngineCase):
    def test_create_apply_export_install(self):
        source = self.make_project(files={"keep.txt": "same", "edit.txt": "old text", "gone.txt": "bye",
                                          "code/a.smali": "const v0, 0x1\n"})
        (source.src / "edit.txt").write_text("new text")
        (source.src / "gone.txt").unlink()
        (source.src / "added" / "n.txt").parent.mkdir()
        (source.src / "added" / "n.txt").write_text("fresh")
        changes = source.changes()
        plugin = plugins.create(
            {"id": "tester.sample", "name": "Sample", "author": "tester"}, source,
            files=changes["added"] + changes["modified"], deleted=changes["deleted"],
            rules=[{"files": "code/**/*.smali", "find": "0x1", "replace": "0x0"}])
        self.assertEqual(plugin.summary(), "1 copy, 1 delete, 1 replace")

        archive = plugins.export(plugin, self.tmp / "sample.sdplugin")
        plugins.remove(plugin)
        self.assertEqual(plugins.list_installed(), ([], []))
        plugin = plugins.install_file(archive)
        self.assertEqual(plugin.id, "tester.sample")

        target = self.make_project(files={"keep.txt": "same", "edit.txt": "old text", "gone.txt": "bye",
                                          "code/a.smali": "const v0, 0x1\n"})
        report = plugins.apply(plugin, target, self.ctx)
        self.assertEqual(len(report), 3)
        self.assertEqual((target.src / "edit.txt").read_text(), "new text")
        self.assertEqual((target.src / "added" / "n.txt").read_text(), "fresh")
        self.assertFalse((target.src / "gone.txt").exists())
        self.assertEqual((target.src / "code" / "a.smali").read_text(), "const v0, 0x0\n")
        self.assertEqual(target.meta["applied_plugins"][0]["id"], "tester.sample")

    def test_precheck_stops_before_changes(self):
        project = self.make_project(files={"a.smali": "nothing relevant", "b.txt": "x"})
        directory = config.plugins_dir() / "strict"
        directory.mkdir(parents=True)
        config.write_json(directory / "plugin.json", {
            "format": 1, "id": "strict", "name": "Strict",
            "operations": [{"op": "delete", "path": "b.txt"},
                           {"op": "replace", "files": "*.smali", "find": "missing", "replace": "x"}]})
        with self.assertRaises(plugins.PluginError):
            plugins.apply(plugins.load(directory), project, self.ctx)
        self.assertTrue((project.src / "b.txt").exists())  # nothing was touched

    def test_optional_and_regex_rules(self):
        project = self.make_project(files={"a.smali": "const/4 v0, 0x1\nconst/4 v1, 0x1\n"})
        directory = config.plugins_dir() / "regex"
        directory.mkdir(parents=True)
        config.write_json(directory / "plugin.json", {
            "format": 1, "id": "regex", "name": "Regex",
            "operations": [{"op": "replace", "files": "*.smali", "find": "absent", "replace": "", "required": False},
                           {"op": "replace", "files": "*.smali", "regex": True,
                            "find": r"const/4 (v\d), 0x1", "replace": r"const/4 \1, 0x0"}]})
        plugins.apply(plugins.load(directory), project, self.ctx)
        self.assertEqual((project.src / "a.smali").read_text(), "const/4 v0, 0x0\nconst/4 v1, 0x0\n")

    def test_validation(self):
        good = {"format": 1, "id": "ok.id", "name": "x", "operations": []}
        plugins.validate_manifest(dict(good))
        bad_cases = [
            {**good, "id": "has space"},
            {**good, "format": 99},
            {**good, "target": {"type": "exe"}},
            {**good, "operations": [{"op": "run", "command": "rm -rf /"}]},
            {**good, "operations": [{"op": "delete", "path": "../outside"}]},
            {**good, "operations": [{"op": "delete", "path": "."}]},
            {**good, "operations": [{"op": "copy", "from": "/etc/passwd", "to": "x"}]},
            {**good, "operations": [{"op": "replace", "files": "*", "find": "(", "replace": "", "regex": True}]},
        ]
        for manifest in bad_cases:
            with self.assertRaises(plugins.PluginError, msg=manifest):
                plugins.validate_manifest(manifest)

    def test_compatibility(self):
        apk = self.make_apk_project()
        directory = config.plugins_dir() / "jaronly"
        directory.mkdir(parents=True)
        config.write_json(directory / "plugin.json", {
            "format": 1, "id": "jaronly", "name": "x", "target": {"type": "jar"}, "operations": []})
        self.assertTrue(plugins.compatibility(plugins.load(directory), apk))
        config.write_json(directory / "plugin.json", {
            "format": 1, "id": "jaronly", "name": "x", "target": {"type": "apk", "package": "com.example.*"},
            "operations": []})
        self.assertEqual(plugins.compatibility(plugins.load(directory), apk), [])

    def test_code_type(self):
        # stated by the author
        base = {"format": 1, "id": "c.one", "name": "x", "operations": []}
        directory = config.plugins_dir() / "c.one"
        directory.mkdir(parents=True)
        config.write_json(directory / "plugin.json", {**base, "code": "java"})
        self.assertEqual(plugins.load(directory).code, "java")
        with self.assertRaises(plugins.PluginError):
            plugins.validate_manifest({**base, "code": "kotlin"})

        # worked out from what the plugin touches when it does not say
        def code_of(operations, files=()):
            for rel in files:
                (directory / rel).parent.mkdir(parents=True, exist_ok=True)
                (directory / rel).write_text("x")
            config.write_json(directory / "plugin.json", {**base, "operations": operations})
            return plugins.load(directory).code

        self.assertEqual(code_of([{"op": "replace", "files": "smali/**/*.smali", "find": "a", "replace": "b"}]), "smali")
        self.assertEqual(code_of([{"op": "delete", "path": "smali/classes/com/ads"}]), "smali")
        self.assertEqual(code_of([{"op": "delete", "path": "com/example/Ads.class"}]), "java")
        self.assertEqual(code_of([{"op": "replace", "files": "resources/**/strings.xml", "find": "a", "replace": "b"}]), "none")
        self.assertEqual(code_of([{"op": "copy", "from": "files", "to": ""}],
                                 files=["files/smali/classes/a/B.smali", "files/res/x.png"]), "smali")
        self.assertEqual(code_of([{"op": "copy", "from": "payload", "to": "com/example"}],
                                 files=["payload/Main.class"]), "java")

    def test_code_type_is_recorded_on_create(self):
        apk = self.make_apk_project()
        (apk.src / "smali/classes/com/example/app/MainActivity.smali").write_text("changed")
        made = plugins.create({"id": "c.smali", "name": "s"}, apk, files=apk.changes()["modified"])
        self.assertEqual(made.manifest["code"], "smali")
        self.assertEqual(made.code_label, "Smali")
        made = plugins.create({"id": "c.res", "name": "r"}, apk,
                              rules=[{"files": "resources/**/strings.xml", "find": "Example", "replace": "E"}])
        self.assertEqual(made.manifest["code"], "none")
        made = plugins.create({"id": "c.forced", "name": "f", "code": "smali"}, apk,
                              rules=[{"files": "AndroidManifest.xml", "find": "a", "replace": "b"}])
        self.assertEqual(made.manifest["code"], "smali")  # the author's choice wins

    def test_code_type_decides_where_a_plugin_fits(self):
        apk = self.make_apk_project()
        jar = self.make_project("jar", {"com/example/Main.class": b"\xca\xfe\xba\xbe", "META-INF/MANIFEST.MF": "x"})
        raw_apk = self.make_project("apk", {"AndroidManifest.xml": MANIFEST, "root/classes.dex": b"dex"})
        directory = config.plugins_dir() / "c.fit"
        directory.mkdir(parents=True)

        def problems(code, project):
            config.write_json(directory / "plugin.json",
                              {"format": 1, "id": "c.fit", "name": "x", "code": code, "operations": []})
            return plugins.compatibility(plugins.load(directory), project)

        self.assertEqual(problems("smali", apk), [])
        self.assertTrue(problems("smali", jar))       # a JAR has no smali
        self.assertTrue(problems("smali", raw_apk))   # unpacked without smali
        self.assertEqual(problems("java", jar), [])
        self.assertTrue(problems("java", apk))        # Java is read-only in an APK
        self.assertEqual(problems("none", apk), [])
        self.assertEqual(problems("none", jar), [])

    def test_malicious_archive(self):
        archive = self.tmp / "evil.sdplugin"
        with zipfile.ZipFile(archive, "w") as z:
            z.writestr("plugin.json", json.dumps({"format": 1, "id": "evil", "name": "x", "operations": []}))
            z.writestr("../../escaped.txt", "x")
        with self.assertRaises(JobError):
            plugins.install_file(archive)
        self.assertFalse((self.tmp / "escaped.txt").exists())
        self.assertEqual(plugins.list_installed()[0], [])
        with open(archive, "w") as fh:
            fh.write("not a zip")
        with self.assertRaises(plugins.PluginError):
            plugins.install_file(archive)

    def test_copy_cannot_leave_project(self):
        project = self.make_project(files={"a.txt": "x"})
        os.symlink(self.tmp, project.src / "link")
        directory = config.plugins_dir() / "sneaky"
        (directory / "files").mkdir(parents=True)
        (directory / "files" / "p.txt").write_text("payload")
        config.write_json(directory / "plugin.json", {
            "format": 1, "id": "sneaky", "name": "x",
            "operations": [{"op": "copy", "from": "files/p.txt", "to": "link/sub/p.txt"}]})
        with self.assertRaises(JobError):
            plugins.apply(plugins.load(directory), project, self.ctx)
        self.assertFalse((self.tmp / "sub").exists())


class IdentityTests(EngineCase):
    def test_read(self):
        project = self.make_apk_project()
        current = identity.read(project)
        self.assertEqual(current.package, "com.example.app")
        self.assertEqual(current.label, "Example")
        self.assertEqual((current.version_name, current.version_code), ("1.2", "7"))
        self.assertEqual(current.icon_refs, [("mipmap", "ic_launcher"), ("mipmap", "ic_launcher_round")])
        self.assertTrue(current.icon_file.endswith("mipmap-xhdpi/ic_launcher.png"))
        self.assertEqual(project.meta["package"], "com.example.app")

    def test_rename_everything(self):
        project = self.make_apk_project()
        identity.apply(project, label='Tom & "Jerry\'s" <App>', package="org.other.name",
                       version_name="2.0", version_code="8")
        text = project.manifest_path.read_text()
        self.assertIn('package="org.other.name"', text)
        self.assertIn('android:versionName="2.0"', text)
        self.assertIn('android:versionCode="8"', text)
        # classes are not moved, so relative names are pinned to the old package
        self.assertIn('android:name="com.example.app.App"', text)
        self.assertIn('android:name="com.example.app.Settings"', text)
        self.assertIn('android:name="com.example.app.MainActivity"', text)
        # things that would clash with the original app follow the new package
        self.assertIn('android:authorities="org.other.name.files;org.other.name.other"', text)
        self.assertIn('android:readPermission="org.other.name.permission.SYNC"', text)
        self.assertEqual(text.count("org.other.name.permission.SYNC"), 3)
        self.assertIn('android:name="android.permission.INTERNET"', text)
        # unrelated attributes are left alone
        self.assertIn('android:name="com.example.app.KEY" android:value="com.example.app"', text)
        info = json.loads((project.src / "resources/package_1/package.json").read_text())
        self.assertEqual(info["package_name"], "org.other.name")

        current = identity.read(project)
        self.assertEqual(current.label, 'Tom & "Jerry\'s" <App>')
        self.assertEqual(current.package, "org.other.name")
        strings = (project.src / "resources/package_1/res/values/strings.xml").read_text()
        self.assertIn('<string name="app_name">Tom &amp; "Jerry\'s" &lt;App&gt;</string>', strings)
        self.assertIn('<string name="other">Example</string>', strings)
        german = (project.src / "resources/package_1/res/values-de/strings.xml").read_text()
        self.assertIn("Tom &amp;", german)

    def test_default_language_only(self):
        project = self.make_apk_project()
        identity.apply(project, label="Renamed", all_locales=False)
        german = (project.src / "resources/package_1/res/values-de/strings.xml").read_text()
        self.assertIn("Beispiel", german)

    def test_keep_references(self):
        project = self.make_apk_project()
        identity.apply(project, package="org.other.name", rename_refs=False)
        text = project.manifest_path.read_text()
        self.assertIn('package="org.other.name"', text)
        self.assertIn('android:authorities="com.example.app.files;com.example.app.other"', text)

    def test_rejects_bad_input(self):
        project = self.make_apk_project()
        before = project.manifest_path.read_text()
        for kwargs in ({"package": "nodots"}, {"package": "1bad.name"}, {"package": "a..b"},
                       {"version_code": "1.5"}, {"label": "   "}):
            with self.assertRaises(JobError, msg=kwargs):
                identity.apply(project, **kwargs)
        self.assertEqual(project.manifest_path.read_text(), before)

    def test_icon(self):
        project = self.make_apk_project()
        self.assertEqual(identity.icon_sizes(project), [48, 72, 96, 144, 192])
        icons = self.tmp / "icons"
        icons.mkdir()
        for size in identity.icon_sizes(project):
            (icons / f"{size}.png").write_bytes(png(size, size))
        identity.apply(project, icon_dir=str(icons))
        res = project.src / "resources/package_1/res"
        self.assertEqual(identity._image_size(res / "mipmap-mdpi/ic_launcher.png"), (48, 48))
        self.assertEqual(identity._image_size(res / "mipmap-xhdpi/ic_launcher.png"), (96, 96))
        # adaptive definitions would override the new images, so they are removed...
        self.assertFalse((res / "mipmap-anydpi-v26/ic_launcher.xml").exists())
        self.assertFalse((res / "mipmap-anydpi-v26/ic_launcher_round.xml").exists())
        # ...and an icon that only existed as a definition gets real images
        self.assertEqual(identity._image_size(res / "mipmap-xxxhdpi/ic_launcher_round.png"), (192, 192))

    def test_icon_needs_every_size(self):
        project = self.make_apk_project()
        icons = self.tmp / "icons"
        icons.mkdir()
        (icons / "48.png").write_bytes(png(48, 48))
        with self.assertRaises(JobError):
            identity.apply(project, icon_dir=str(icons))
        res = project.src / "resources/package_1/res"
        self.assertTrue((res / "mipmap-anydpi-v26/ic_launcher.xml").exists())  # untouched on failure


@unittest.skipUnless(tools.find_java() and tools.jartool(), "needs Java and a built shiftdex-jartool.jar")
class JarTests(EngineCase):
    def make_jar(self):
        path = self.tmp / "sample.jar"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("com/example/Hello.txt", "hello\n")
            z.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\r\nMain-Class: com.example.Main\r\n"
                                               "Implementation-Title: Sample\r\n\r\n")
            z.writestr("META-INF/SAMPLE.SF", "Signature-Version: 1.0\r\n")
            z.writestr("META-INF/SAMPLE.RSA", b"\x00\x01")
            z.writestr(zipfile.ZipInfo("assets/raw.bin"), b"\x00" * 64, compress_type=zipfile.ZIP_STORED)
            z.writestr("../evil.txt", "should never be extracted")
        return path

    def test_round_trip(self):
        project = Project.create(self.tmp / "ws", self.make_jar(), self.settings, self.ctx)
        self.assertEqual(project.kind, "jar")
        self.assertEqual(project.meta["main_class"], "com.example.Main")
        self.assertEqual(project.meta["label"], "Sample")
        self.assertFalse((self.tmp / "ws" / "evil.txt").exists())
        self.assertFalse((project.dir / "evil.txt").exists())
        self.assertEqual(project.changes(), {"added": [], "modified": [], "deleted": []})

        (project.src / "com/example/Hello.txt").write_text("edited\n")
        (project.src / "extra.txt").write_text("new\n")
        out = self.tmp / "out.jar"
        result = project.build(self.settings, self.ctx, out)
        self.assertFalse(result["signed"])
        with zipfile.ZipFile(out) as z:
            names = z.namelist()
            self.assertEqual(names[0], "META-INF/MANIFEST.MF")  # required by the jar format
            self.assertEqual(z.read("com/example/Hello.txt"), b"edited\n")
            self.assertIn("extra.txt", names)
            self.assertNotIn("META-INF/SAMPLE.SF", names)       # stale signature is left out
            self.assertNotIn("META-INF/SAMPLE.RSA", names)
            self.assertEqual(z.getinfo("assets/raw.bin").compress_type, zipfile.ZIP_STORED)
            self.assertIsNone(z.testzip())

        self.settings.set("jar_strip_signatures", False)
        project.build(self.settings, self.ctx, out)
        with zipfile.ZipFile(out) as z:
            self.assertIn("META-INF/SAMPLE.SF", z.namelist())

    def test_build_refuses_output_inside_source(self):
        project = Project.create(self.tmp / "ws", self.make_jar(), self.settings, self.ctx)
        with self.assertRaises(JobError):
            project.build(self.settings, self.ctx, project.src / "out.jar")

    def test_not_an_archive(self):
        bogus = self.tmp / "bogus.jar"
        bogus.write_text("this is not a zip file")
        with self.assertRaises(JobError):
            Project.create(self.tmp / "ws", bogus, self.settings, self.ctx)
        self.assertEqual(list((self.tmp / "ws").iterdir()), [])  # the half-made project is cleaned up


if __name__ == "__main__":
    unittest.main()
