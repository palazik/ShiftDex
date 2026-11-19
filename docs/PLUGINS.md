# Making ShiftDex plugins

A plugin is a set of changes packed so that someone else can apply it to
their own copy of an app. You unpack an APK or JAR, apply the plugin, build,
and you have the modified app, without redoing the work by hand.

This guide has two parts: a tutorial that takes you from nothing to a
shareable plugin, and a reference for the `plugin.json` format.

- [What a plugin can and cannot do](#what-a-plugin-can-and-cannot-do)
- [Smali plugins and Java plugins](#smali-plugins-and-java-plugins)
- [Tutorial 1: a plugin from your own changes](#tutorial-1-a-plugin-from-your-own-changes)
- [Tutorial 2: writing a plugin by hand](#tutorial-2-writing-a-plugin-by-hand)
- [Making plugins that survive app updates](#making-plugins-that-survive-app-updates)
- [Testing a plugin](#testing-a-plugin)
- [Sharing a plugin](#sharing-a-plugin)
- [Reference: plugin.json](#reference-pluginjson)
- [Reference: operations](#reference-operations)
- [Reference: file patterns](#reference-file-patterns)
- [Where things are in an unpacked project](#where-things-are-in-an-unpacked-project)
- [Troubleshooting](#troubleshooting)

## What a plugin can and cannot do

A plugin can do three things to an unpacked project:

- **copy** files into it (adding new files or replacing existing ones),
- **delete** files or folders from it,
- **replace** text inside its files.

That is all. A plugin is a description of changes, not a program. It cannot
run commands or scripts, and it cannot read or write anything outside the
project it is applied to. This is on purpose: you can apply a plugin from a
stranger and the worst it can do is break that one project, which you can
undo with a snapshot.

ShiftDex takes a snapshot of the project before applying a plugin (you can
turn this off in Preferences, but there is rarely a reason to).

## Smali plugins and Java plugins

Every plugin has a **code type**. It says what kind of code the plugin
changes, and ShiftDex always shows it: in the plugin list, when a plugin is
installed, and in the question it asks before applying one.

| Code type | The plugin changes | Can be applied to |
| --- | --- | --- |
| `smali` | the smali code of an APK | APK projects unpacked with smali (the default) |
| `java` | Java class files (`.class`) of a JAR | JAR projects |
| `none` | no code: images, strings, layouts, the manifest, assets | any project its other rules allow |

Why there is no Java plugin for APKs: the Java you see in an APK project's
Java view is decompiled for reading and cannot be built back into the app.
Changes to an APK's code are always made in smali, so a plugin that changes
an APK's code is a smali plugin. Use the Java view to find what to change,
then write the plugin against the smali.

You normally do not have to set the code type yourself. **New Plugin** works
it out from the files and rules you include (anything under `smali/` or
ending in `.smali` makes a smali plugin; `.class` and `.java` files make a
Java plugin) and tells you the result. You can also choose it in the
**Code type** box. In a hand-written plugin, set `"code"` in `plugin.json`.

## Tutorial 1: a plugin from your own changes

The quickest way to make a plugin is to make the changes yourself and let
ShiftDex pack them up.

1. Open the APK or JAR in ShiftDex.
2. Make your changes: edit smali or XML, replace images, delete what you do
   not want, add files. Save everything.
3. Build the project and check on a device that the result does what you
   want. A plugin made from broken changes is a broken plugin.
4. Open **Project > Plugins** and press **New Plugin**.
5. Fill in the form:
   - **Name**: what people will see in their plugin list.
   - **ID**: a unique name with no spaces, by convention
     `yourname.what-it-does`. ShiftDex suggests one from the name. Installing
     a plugin replaces any installed plugin with the same ID, so keep the ID
     the same between versions of your plugin and different between plugins.
   - **Version**, **Author**, **Description**: free text.
   - **Code type**: leave it on "Detect from the changes below" unless you
     have a reason to set it by hand.
6. Under **Changed files to include**, ShiftDex lists every file that
   differs from the original: added, modified and deleted. Untick anything
   that is not part of what you want to share (a file you touched by
   accident, a debugging change).
7. Leave **Only allow this plugin on …** ticked if the plugin only makes
   sense for this one app. Untick it for plugins that should work on any
   app.
8. Press **Create Plugin**. ShiftDex tells you whether it made a smali
   plugin, a Java plugin or one without code.

The plugin is now installed on your machine. To hand it to someone, select
it in the plugin list and press **Export**. You get a single `.sdplugin`
file.

What you get this way is a plugin that ships whole files: every modified
file is stored complete and copied over the other person's file. That is
simple and reliable when they have the same version of the app. When their
version differs, a whole-file plugin overwrites the differences. For those
cases, read on.

## Tutorial 2: writing a plugin by hand

Writing the plugin yourself gives you control over exactly what happens.
We will make a plugin for an imaginary app that:

- changes the app's "About" text,
- turns off a network call in the code,
- replaces the splash image,
- removes an analytics folder.

### 1. Create the folder

A plugin is a folder with a `plugin.json` in it, plus any files it ships.

```
my-first-plugin/
├── plugin.json
└── files/
    └── splash.png
```

Put the new splash image at `files/splash.png`. The folder name `files` is
only a convention; you refer to it yourself from `plugin.json`.

### 2. Write plugin.json

```json
{
  "format": 1,
  "id": "yourname.my-first-plugin",
  "name": "My first plugin",
  "version": "1.0",
  "author": "yourname",
  "description": "New About text and splash image, no analytics.",
  "code": "smali",
  "target": {
    "type": "apk",
    "package": "com.example.app"
  },
  "operations": [
    {
      "op": "replace",
      "name": "About text",
      "files": "resources/*/res/values*/strings.xml",
      "find": "<string name=\"about_text\">Made by Example Inc.</string>",
      "replace": "<string name=\"about_text\">Modified with ShiftDex.</string>"
    },
    {
      "op": "replace",
      "name": "Disable update check",
      "files": "smali/**/UpdateChecker.smali",
      "regex": true,
      "find": "const-string (v\\d+), \"https://example\\.com/version\"",
      "replace": "const-string \\1, \"\""
    },
    {
      "op": "copy",
      "from": "files/splash.png",
      "to": "resources/package_1/res/drawable-xxhdpi/splash.png"
    },
    {
      "op": "delete",
      "path": "smali/classes/com/example/analytics"
    }
  ]
}
```

Reading it top to bottom:

- `code` says this is a smali plugin: two of its operations change smali.
  People are told so before they install or apply it.
- `target` says this is for APK projects of `com.example.app` only. ShiftDex
  will not offer to apply it to anything else.
- The first `replace` looks at the strings file of every language
  (`values*`) and swaps one exact line for another.
- The second `replace` uses a regular expression, because the register name
  (`v0`, `v3`, …) can differ between builds of the app. `\\1` puts back
  whichever register was there. Backslashes are doubled because this is
  JSON.
- `copy` puts the image from the plugin into the project, replacing the
  original.
- `delete` removes a folder and everything in it.

Operations run in the order they are written.

### 3. Install it

Copy the folder into the plugins folder:

```
~/.local/share/shiftdex/plugins/
```

The **Open Folder** button in the Plugins window takes you there. Close and
reopen the Plugins window and your plugin is in the list. If there is a
mistake in `plugin.json`, the plugin is listed as "cannot be loaded" and
selecting it shows what is wrong.

While you are developing, edit the files right there in the plugins folder.
Changes take effect the next time you open the Plugins window.

### 4. Apply it

Open the app the plugin is for, go to **Project > Plugins**, select the
plugin and press **Apply to Project**. ShiftDex reports what each operation
did:

```
Replaced 1 occurrence(s) in 1 file(s): About text
Replaced 2 occurrence(s) in 1 file(s): Disable update check
Copied 1 file(s) to resources/package_1/res/drawable-xxhdpi/splash.png
Deleted 1 item(s) matching smali/classes/com/example/analytics
```

Then build and test the app.

### A complete example you can run

Here is a small plugin that works on any APK without changes, so you can
try the whole cycle. It sets `android:allowBackup` to `false` where the
manifest says `true`, and adds a text file to the app's assets.

```json
{
  "format": 1,
  "id": "example.no-backup",
  "name": "Disallow backup",
  "version": "1.0",
  "author": "example",
  "description": "Sets android:allowBackup to false and adds a note to the assets.",
  "code": "none",
  "target": { "type": "apk" },
  "operations": [
    {
      "op": "replace",
      "name": "allowBackup",
      "files": "AndroidManifest.xml",
      "find": "android:allowBackup=\"true\"",
      "replace": "android:allowBackup=\"false\"",
      "required": false
    },
    {
      "op": "copy",
      "from": "files/modified.txt",
      "to": "root/assets/modified.txt"
    }
  ]
}
```

Save it as `plugin.json` in a new folder inside the plugins folder, create
`files/modified.txt` next to it with any text, and apply it to an app.
`"required": false` means the plugin still applies to apps that do not have
`allowBackup="true"`; without it, such an app would be refused.

## Making plugins that survive app updates

The app you modify today gets an update next month. Whether your plugin
still works depends on how it was written.

**Prefer replace rules over whole files for code.** A whole smali file
copied over the new version throws away everything that changed in that
file. A replace rule that finds one instruction and changes it keeps
working as long as that instruction is still there.

**Match as little as you need.** A rule that finds twenty lines of smali
breaks when any of them change. Find the one or two lines that matter.

**Use regular expressions for things that move.** Register numbers
(`v0`, `p1`), line numbers (`.line 42`) and obfuscated names change between
builds. `v\\d+` matches any register.

**Obfuscated names are the hard part.** Many apps are processed by R8, which
renames classes to things like `La/b/c;`, differently in every release. A
rule that names `La/b/c;` will stop matching. Search for something stable
instead: a string constant, a call to an Android or library method that is
not renamed, a resource name.

**Whole files are fine for resources.** Images, layouts, fonts and raw
assets are usually replaced as a whole anyway.

**Decide what happens when a rule finds nothing.** By default a replace rule
that matches nothing stops the plugin, because it usually means the app
changed and the plugin no longer fits. Set `"required": false` for rules
that are optional.

In the New Plugin window you can combine both styles: untick the smali
files in the changed-files list and add replace rules for them instead, and
keep the resource files ticked.

## Testing a plugin

Test on an untouched copy of the app, not on the project you made the plugin
from: that project already has the changes, so replace rules would find
nothing.

1. Open the original APK or JAR again. ShiftDex creates a second project.
   (Or, in your existing project: **Project > Backups > Original File >
   Reset Project to Original**.)
2. Apply the plugin. Read the report: does each operation report the number
   of changes you expect? A replace that hit 40 places when you meant one
   is a rule that is too loose.
3. Build, install on a device or emulator, and try the parts you changed.

If something went wrong, **Project > Backups** has the snapshot taken just
before the plugin was applied. Restore it, fix the plugin and try again.

From a terminal, the same checks can be scripted:

```
python3 -m shiftdex.cli plugins --project <project folder>
python3 -m shiftdex.cli plugin-apply <project folder> yourname.my-first-plugin
```

(See the README for how to run the engine from a terminal.)

## Sharing a plugin

Select the plugin in the Plugins window and press **Export**. The
`.sdplugin` file you get is a zip archive of the plugin folder. You can
also make it yourself:

```
cd my-first-plugin
zip -r ../my-first-plugin.sdplugin .
```

The archive must have `plugin.json` at its top level (an archive that wraps
everything in one folder is accepted too).

The other person installs it with **Install…** in the Plugins window.

Things to keep in mind when you publish plugins:

- A plugin that ships whole files contains parts of the app it was made
  from. Only distribute what you are allowed to distribute. Plugins made of
  replace rules and your own files do not have this problem.
- Say which app version the plugin was made for. It is the first thing
  people will ask when it does not apply.
- Raise `version` when you change the plugin, and keep the `id`.

## Reference: plugin.json

```json
{
  "format": 1,
  "id": "yourname.plugin-name",
  "name": "Shown in the plugin list",
  "version": "1.0",
  "author": "yourname",
  "description": "One or two sentences.",
  "code": "smali",
  "target": { "type": "apk", "package": "com.example.*" },
  "operations": []
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `format` | no | Version of the plugin format. Currently `1`, which is also the default. A plugin with a higher number than your ShiftDex understands is refused. |
| `id` | yes | Unique name, 2 to 64 characters: letters, digits, `.`, `-`, `_`, starting with a letter or digit. Also the name of the plugin's folder once installed. |
| `name` | no | Display name. Defaults to the ID. |
| `version` | no | Free text, shown in the list. |
| `author` | no | Free text. |
| `description` | no | Shown when the plugin is selected. |
| `code` | no | The plugin's [code type](#smali-plugins-and-java-plugins): `"smali"`, `"java"` or `"none"`. When missing, ShiftDex works it out from the operations. State it anyway: it is what users are shown. |
| `target.type` | no | `"apk"`, `"jar"` or `"any"` (default). The plugin is only offered for projects of that type. |
| `target.package` | no | Package name the plugin is for. May contain `*` and `?` wildcards, for example `com.example.*`. Empty or missing means any package. For JARs this is compared with the manifest's `Automatic-Module-Name` or `Bundle-SymbolicName`. |
| `operations` | yes | List of operations, run in order. May be empty. |

Unknown fields are ignored, so you can add your own (a `homepage`, notes
for yourself).

## Reference: operations

Every operation is an object with an `"op"` field.

### copy

Copies a file or a folder from the plugin into the project. Existing files
are overwritten; missing folders are created.

```json
{ "op": "copy", "from": "files", "to": "" }
```

| Field | Required | Meaning |
| --- | --- | --- |
| `from` | yes | Path inside the plugin folder: a file or a folder. |
| `to` | no | Path inside the project. Default `""`, the project's top folder. |

How `to` is read:

- `from` is a **folder**: its contents are merged into the folder `to`.
  `{"from": "files", "to": ""}` lays the tree under `files/` over the whole
  project. This is what plugins made with **New Plugin** use.
- `from` is a **file** and `to` is an existing folder, is empty, or ends
  with `/`: the file is copied into that folder under its own name.
- `from` is a **file** and `to` is anything else: `to` is the new file's
  full path, so you can rename while copying.

### delete

Deletes files or folders from the project. Folders are deleted with
everything in them. Nothing happens if there is nothing to delete.

```json
{ "op": "delete", "path": "smali/classes/com/example/ads" }
```

| Field | Required | Meaning |
| --- | --- | --- |
| `path` | yes | A path or a [pattern](#reference-file-patterns) inside the project. |

### replace

Replaces text in every file that matches a pattern. Files that are not
valid UTF-8 text, and files larger than 8 MB, are skipped.

```json
{
  "op": "replace",
  "name": "Remove the nag screen",
  "files": "smali/**/*.smali",
  "find": "invoke-static {}, Lcom/example/Nag;->show()V",
  "replace": "",
  "regex": false,
  "ignore_case": false,
  "required": true
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `files` | yes | A path or [pattern](#reference-file-patterns) for the files to look in. A folder means every file under it. |
| `find` | yes | The text to look for. With `regex`, a regular expression. |
| `replace` | no | What to put in its place. Default: nothing, which deletes the text. With `regex`, `\1`, `\2`, … insert the matched groups. |
| `regex` | no | Treat `find` as a regular expression. Default `false`. |
| `ignore_case` | no | Ignore upper and lower case. Default `false`. |
| `required` | no | When `true` (the default), the plugin stops with an error if the text is not found anywhere. Set to `false` for optional changes. |
| `name` | no | A short label used in reports and error messages instead of the search text. |

Notes on regular expressions:

- The syntax is Python's `re` module. `^` and `$` match at the start and
  end of each line. `.` does not match a line break; write `\n` to match
  across lines.
- In JSON every backslash is written twice: the expression `\d+` is
  `"\\d+"`, and a literal dot is `"\\."`.
- Without `regex`, the text is matched exactly as written, including spaces
  and line breaks (`\n` in JSON), so nothing needs escaping apart from JSON's
  own `\"` and `\\`.

### What happens when an operation fails

Before changing anything, ShiftDex checks that every file a `copy` needs is
in the plugin, and that every required `replace` finds its text. If not,
the plugin is refused and the project is left untouched.

One case cannot be checked ahead: a required `replace` that comes after a
`copy`, because the copy may bring in the very files the replace is meant
for. If such a replace finds nothing, the plugin stops there with the
earlier operations already applied. Restore the snapshot taken before the
plugin to undo them.

## Reference: file patterns

Paths are always relative to the project's top folder, use `/`, and may not
contain `..` or start with `/`.

| Pattern | Matches |
| --- | --- |
| `AndroidManifest.xml` | exactly that file |
| `smali/classes/com/example` | that folder (and, for `replace` and `delete`, everything in it) |
| `*.json` | files ending in `.json` in the top folder |
| `resources/*/res/values*/strings.xml` | `strings.xml` in every `values` folder of every resource package |
| `smali/**/*.smali` | every `.smali` file at any depth under `smali` |
| `smali/**/Config.smali` | every `Config.smali`, wherever it is |
| `res?/x.png` | `?` matches exactly one character |
| `lib/[ax]*/libfoo.so` | `[...]` matches one of the listed characters |

`*` and `?` never match a `/`. `**` matches any number of folders,
including none.

## Where things are in an unpacked project

Paths in a plugin refer to the unpacked project as you see it in the file
list.

**APK** (decoded by APKEditor as XML, the default):

| Path | Content |
| --- | --- |
| `AndroidManifest.xml` | the manifest, as readable XML |
| `smali/classes/…` | code of `classes.dex` as smali, one file per class |
| `smali/classes2/…` | code of `classes2.dex`, and so on |
| `resources/package_1/res/values*/` | strings, colours, styles and other values |
| `resources/package_1/res/<type>-<config>/` | images, layouts, raw files (`drawable-xxhdpi`, `layout`, `mipmap-anydpi-v26`, …) |
| `resources/package_1/package.json` | name and ID of the resource package |
| `root/` | everything else from the APK as it was: `assets/`, `lib/`, `META-INF/`, `kotlin/` |
| `path-map.json`, `archive-info.json`, `uncompressed-files.json` | APKEditor's notes for rebuilding. Leave them alone unless you know why. |

Resource files appear under their resource names
(`res/drawable-xxhdpi/splash.png`) even when the APK stored them under
shortened paths. APKEditor restores the original paths when building.

**JAR**: the archive's own layout, unchanged. `META-INF/MANIFEST.MF` is the
manifest; `.class` files are compiled code and cannot be edited as text, so
a plugin can only replace or delete them as whole files.

## Troubleshooting

**The plugin is listed as "cannot be loaded".** Select it; the message
below the list says which field is wrong. The most common causes are a JSON
syntax error (a missing comma, a single backslash) and an `id` with spaces.

**"A smali plugin: …" / "A Java plugin: …".** The plugin's code type does
not fit the open project. A smali plugin needs an APK project that has a
`smali` folder; if yours has none, "Decode DEX code to smali" was turned
off in Preferences when the APK was opened. A Java plugin needs a JAR
project.

**"Made for APK projects, this one is JAR" / "Made for package …".** The
plugin's `target` does not match the open project. If you know better, edit
`target` in the plugin's `plugin.json`.

**"Nothing to replace for …".** The text is not in the files the rule looks
at. Use **Search in All Files** with the same text to see whether it exists
at all, and check the `files` pattern. Remember that text is matched
exactly: indentation, the kind of quotes, and line endings count.
APKEditor writes `AndroidManifest.xml` with Windows line endings (`\r\n`),
so a `find` that spans several lines of the manifest needs `\r\n` between
them, or a regular expression with `\r?\n`. The status bar shows `CRLF`
for files that use them.

**A replace rule changed far more than intended.** The rule is too loose.
Restore the snapshot, then narrow `files` to the one file that should
change or make `find` longer.

**The app crashes after applying a plugin.** Usually a smali edit that left
the code inconsistent, for example removing a call whose result the next
instruction uses. Open the smali file and read around the change; `adb
logcat` on the device names the class and method that failed verification.

**The build fails after applying a plugin.** Open **Help > Last Task Log**
to see APKEditor's message. A common cause is a `copy` that put a file in a
`res` folder under a name Android does not allow (resource file names are
lower case letters, digits and underscores).
