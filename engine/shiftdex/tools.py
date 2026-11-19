# SPDX-License-Identifier: GPL-3.0-or-later
"""External tools: Java, APKEditor and the APK signers.

APKEditor (https://github.com/REAndroid/APKEditor) is not bundled. It is
downloaded from its GitHub releases into the user's data directory, so the
app always works with the current upstream release.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

from . import __version__, config
from .runner import JobError

APKEDITOR_REPO = "REAndroid/APKEditor"
SIGNER_REPO = "patrickfav/uber-apk-signer"
JADX_REPO = "skylot/jadx"

_UA = {"User-Agent": f"ShiftDex/{__version__}", "Accept": "application/vnd.github+json"}
_TIMEOUT = 30


# -- java ------------------------------------------------------------------

def find_java(settings=None):
    """Path of the java binary to use, or None."""
    custom = settings.get("java_path") if settings else ""
    if custom:
        return custom if os.access(custom, os.X_OK) and os.path.isfile(custom) else None
    return shutil.which("java")


def java_version(java):
    """Version string such as '21.0.4', or None when java can't be run."""
    if not java:
        return None
    try:
        out = subprocess.run(
            [java, "-version"], capture_output=True, text=True, timeout=20, stdin=subprocess.DEVNULL
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r'version "([^"]+)"', out.stderr + out.stdout)
    return match.group(1) if match else None


def java_major(version):
    if not version:
        return 0
    parts = version.split(".")
    try:
        # "1.8.0_392" is Java 8
        return int(parts[1]) if parts[0] == "1" and len(parts) > 1 else int(re.match(r"\d+", parts[0]).group())
    except (ValueError, AttributeError):
        return 0


def java_command(settings):
    java = find_java(settings)
    if not java:
        raise JobError("Java was not found. Install a Java runtime or set its path in Preferences.")
    cmd = [java]
    heap = (settings.get("java_heap") or "").strip()
    if re.fullmatch(r"\d+[mMgG]", heap):
        cmd.append("-Xmx" + heap)
    return cmd


def find_java_tool(name, settings=None):
    """Locate a JDK/JRE companion binary (keytool, javap) next to java."""
    java = find_java(settings)
    if java:
        sibling = Path(os.path.realpath(java)).parent / name
        if sibling.is_file() and os.access(sibling, os.X_OK):
            return str(sibling)
    return shutil.which(name)


# -- registry of downloaded tools -------------------------------------------

def _registry_path():
    return config.tools_dir() / "tools.json"


def _registry():
    data = config.read_json(_registry_path(), {})
    return data if isinstance(data, dict) else {}


def _installed(kind):
    entry = _registry().get(kind)
    if not isinstance(entry, dict):
        return None
    name = str(entry.get("file", ""))
    path = config.tools_dir() / name
    if not name or not path.exists():
        return None
    return {"version": entry.get("version", "?"), "path": path}


def apkeditor():
    """{'version', 'path'} of the installed APKEditor jar, or None."""
    return _installed("apkeditor")


def uber_signer():
    return _installed("signer")


def jadx():
    """{'version', 'path'} of the installed jadx folder, or None."""
    tool = _installed("jadx")
    if tool and not any((tool["path"] / "lib").glob("*.jar")):
        return None
    return tool


def jadx_command(settings):
    tool = jadx()
    if not tool:
        raise JobError("The Java decompiler (jadx) is not installed. Download it in Preferences → Tools.")
    # started through java directly, so the Java path and memory settings apply
    return java_command(settings) + ["-cp", str(tool["path"] / "lib") + "/*", "jadx.cli.JadxCLI"]


def apkeditor_command(settings):
    tool = apkeditor()
    if not tool:
        raise JobError("APKEditor is not installed. Open Preferences → Tools to download it.")
    return java_command(settings) + ["-jar", str(tool["path"])]


def jartool():
    """Path of ShiftDex's own Java helper (jartool/), or None."""
    candidates = []
    env = os.environ.get("SHIFTDEX_JARTOOL")
    if env:
        candidates.append(Path(env))
    here = Path(__file__).resolve().parent
    # installed: <prefix>/lib/shiftdex/engine/shiftdex -> <prefix>/lib/shiftdex
    candidates.append(here.parent.parent / "shiftdex-jartool.jar")
    # source tree after a build
    candidates.append(here.parent.parent / "build" / "lib" / "shiftdex" / "shiftdex-jartool.jar")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def jartool_command(settings):
    path = jartool()
    if not path:
        raise JobError("shiftdex-jartool.jar is missing. Reinstall ShiftDex or run the build again.")
    return java_command(settings) + ["-jar", str(path)]


def find_apksigner():
    found = shutil.which("apksigner")
    if found:
        return found
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        root = os.environ.get(var)
        if not root:
            continue
        candidates = sorted(Path(root).glob("build-tools/*/apksigner"), reverse=True)
        if candidates:
            return str(candidates[0])
    return None


def signer_summary():
    """Human readable description of the signer that would be used."""
    if find_apksigner():
        return "apksigner (Android SDK)"
    tool = uber_signer()
    if tool:
        return f"uber-apk-signer {tool['version']}"
    return None


# -- downloads ---------------------------------------------------------------

def _open(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=_UA), timeout=_TIMEOUT)


def latest_release(repo):
    """Return {'tag', 'assets': [{'name', 'url', 'sha256'}]} for a GitHub repo."""
    try:
        with _open(f"https://api.github.com/repos/{repo}/releases/latest") as resp:
            data = json.load(resp)
        assets = []
        for asset in data.get("assets", []):
            digest = asset.get("digest") or ""
            assets.append({
                "name": asset["name"],
                "url": asset["browser_download_url"],
                "sha256": digest.split(":", 1)[1] if digest.startswith("sha256:") else None,
            })
        return {"tag": data["tag_name"], "assets": assets}
    except (urllib.error.URLError, OSError, ValueError, KeyError) as api_error:
        # The API is rate limited to 60 requests an hour per address. The
        # releases/latest page redirects to the tag, which is enough to go on.
        try:
            with _open(f"https://github.com/{repo}/releases/latest") as resp:
                tag = resp.geturl().rstrip("/").rsplit("/", 1)[-1]
        except (urllib.error.URLError, OSError) as exc:
            raise JobError(f"Could not reach GitHub: {_reason(exc)}") from api_error
        if not tag or tag == "latest":
            raise JobError(f"No release found for {repo}") from api_error
        return {"tag": tag, "assets": []}


def _reason(exc):
    return str(getattr(exc, "reason", exc))


def _download(url, dest, ctx=None, progress=None):
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    digest = hashlib.sha256()
    try:
        with _open(url) as resp, open(tmp, "wb") as out:
            total = int(resp.headers.get("Content-Length") or 0)
            done = 0
            while True:
                if ctx:
                    ctx.check()
                chunk = resp.read(1 << 16)
                if not chunk:
                    break
                out.write(chunk)
                digest.update(chunk)
                done += len(chunk)
                if progress and total:
                    progress(done / total)
    except (urllib.error.URLError, OSError) as exc:
        tmp.unlink(missing_ok=True)
        raise JobError(f"Download failed: {_reason(exc)}") from exc
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
    return tmp, digest.hexdigest()


def _register(kind, version, path):
    registry = _registry()
    old = registry.get(kind, {}).get("file") if isinstance(registry.get(kind), dict) else None
    registry[kind] = {"version": version, "file": path.name}
    config.write_json(_registry_path(), registry)
    if old and old != path.name:
        stale = config.tools_dir() / old
        if stale.is_dir():
            shutil.rmtree(stale, ignore_errors=True)
        else:
            stale.unlink(missing_ok=True)


def install_apkeditor(ctx=None, progress=None):
    """Download the latest APKEditor release. Returns the version tag."""
    log = ctx.log if ctx else (lambda _t: None)
    log(f"Looking up the latest release of {APKEDITOR_REPO} …")
    release = latest_release(APKEDITOR_REPO)
    tag = release["tag"]
    asset = next((a for a in release["assets"] if a["name"].lower().endswith(".jar")), None)
    if not asset:
        name = f"APKEditor-{tag.lstrip('vV')}.jar"
        asset = {
            "name": name,
            "url": f"https://github.com/{APKEDITOR_REPO}/releases/download/{tag}/{name}",
            "sha256": None,
        }
    current = apkeditor()
    if current and current["version"] == tag:
        log(f"APKEditor {tag} is already installed.")
        return tag
    log(f"Downloading {asset['name']} …")
    tmp, sha = _download(asset["url"], config.tools_dir() / asset["name"], ctx, progress)
    if asset["sha256"] and asset["sha256"].lower() != sha:
        tmp.unlink(missing_ok=True)
        raise JobError("The downloaded APKEditor jar does not match its published checksum.")
    _check_jar(tmp)
    final = config.tools_dir() / asset["name"]
    os.replace(tmp, final)
    _register("apkeditor", tag, final)
    log(f"APKEditor {tag} installed (sha256 {sha[:16]}…).")
    return tag


def install_signer(ctx=None, progress=None):
    """Download uber-apk-signer, used when apksigner is not on the system."""
    log = ctx.log if ctx else (lambda _t: None)
    log(f"Looking up the latest release of {SIGNER_REPO} …")
    release = latest_release(SIGNER_REPO)
    tag = release["tag"]
    asset = next((a for a in release["assets"] if a["name"].lower().endswith(".jar")), None)
    if not asset:
        name = f"uber-apk-signer-{tag.lstrip('vV')}.jar"
        asset = {"name": name, "url": f"https://github.com/{SIGNER_REPO}/releases/download/{tag}/{name}"}
    expected = None
    checksum = next((a for a in release["assets"] if "checksum" in a["name"].lower()), None)
    if checksum:
        try:
            with _open(checksum["url"]) as resp:
                match = re.search(r"\b[0-9a-fA-F]{64}\b", resp.read(4096).decode("utf-8", "replace"))
                expected = match.group(0).lower() if match else None
        except (urllib.error.URLError, OSError):
            expected = None
    log(f"Downloading {asset['name']} …")
    tmp, sha = _download(asset["url"], config.tools_dir() / asset["name"], ctx, progress)
    if expected and expected != sha:
        tmp.unlink(missing_ok=True)
        raise JobError("The downloaded signer jar does not match its published checksum.")
    _check_jar(tmp)
    final = config.tools_dir() / asset["name"]
    os.replace(tmp, final)
    _register("signer", tag, final)
    log(f"uber-apk-signer {tag} installed.")
    return tag


def install_jadx(ctx=None, progress=None):
    """Download jadx, the decompiler behind the Java view. Returns the version tag.

    The release zip also carries jadx's own GUI; only the jars are kept.
    """
    import zipfile

    log = ctx.log if ctx else (lambda _t: None)
    log(f"Looking up the latest release of {JADX_REPO} …")
    release = latest_release(JADX_REPO)
    tag = release["tag"]
    version = tag.lstrip("vV")
    asset = next((a for a in release["assets"] if re.fullmatch(r"jadx-[\d.]+\.zip", a["name"])), None)
    if not asset:
        name = f"jadx-{version}.zip"
        asset = {"name": name, "url": f"https://github.com/{JADX_REPO}/releases/download/{tag}/{name}",
                 "sha256": None}
    current = jadx()
    if current and current["version"] == tag:
        log(f"jadx {tag} is already installed.")
        return tag
    log(f"Downloading {asset['name']} (about 70 MB) …")
    archive, sha = _download(asset["url"], config.tools_dir() / asset["name"], ctx, progress)
    try:
        if asset.get("sha256") and asset["sha256"].lower() != sha:
            raise JobError("The downloaded jadx archive does not match its published checksum.")
        final = config.tools_dir() / f"jadx-{version}"
        staging = config.tools_dir() / f".jadx-{os.getpid()}"
        shutil.rmtree(staging, ignore_errors=True)
        (staging / "lib").mkdir(parents=True)
        log("Unpacking …")
        try:
            with zipfile.ZipFile(archive) as bundle:
                for info in bundle.infolist():
                    name = info.filename.replace("\\", "/")
                    # flat list of jars; nothing else from the archive is used
                    if info.is_dir() or not re.fullmatch(r"(?:[^/]+/)?lib/[^/]+\.jar", name):
                        continue
                    if ctx:
                        ctx.check()
                    with bundle.open(info) as src, open(staging / "lib" / name.rsplit("/", 1)[-1], "wb") as out:
                        shutil.copyfileobj(src, out)
            if not any((staging / "lib").glob("jadx-cli*.jar")) and not any((staging / "lib").glob("*.jar")):
                raise JobError("The jadx archive has an unexpected layout.")
            shutil.rmtree(final, ignore_errors=True)
            os.replace(staging, final)
        except zipfile.BadZipFile as exc:
            raise JobError("The downloaded jadx archive is damaged.") from exc
        finally:
            shutil.rmtree(staging, ignore_errors=True)
    finally:
        archive.unlink(missing_ok=True)
    _register("jadx", tag, final)
    log(f"jadx {tag} installed.")
    return tag


def install_local_apkeditor(path):
    """Use an APKEditor jar the user already has (offline machines)."""
    src = Path(path)
    _check_jar(src)
    config.tools_dir().mkdir(parents=True, exist_ok=True)
    dest = config.tools_dir() / src.name
    if src.resolve() != dest.resolve():
        shutil.copyfile(src, dest)
    version = _jar_version(dest) or "local"
    _register("apkeditor", version, dest)
    return version


def _jar_version(jar):
    """Ask an APKEditor jar for its version; 'V1.4.9' or None."""
    java = find_java()
    if not java:
        return None
    try:
        out = subprocess.run([java, "-jar", str(jar), "-version"], capture_output=True, text=True,
                             timeout=30, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"(\d+\.\d+(?:\.\d+)*)", out.stdout + out.stderr)
    return "V" + match.group(1) if match else None


def _check_jar(path):
    import zipfile

    try:
        with zipfile.ZipFile(path) as jar:
            jar.getinfo("META-INF/MANIFEST.MF")
    except (zipfile.BadZipFile, KeyError, OSError) as exc:
        Path(path).unlink(missing_ok=True) if str(path).endswith(".part") else None
        raise JobError("That file is not a valid jar.") from exc
