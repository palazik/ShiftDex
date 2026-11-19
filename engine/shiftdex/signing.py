# SPDX-License-Identifier: GPL-3.0-or-later
"""Sign rebuilt APKs so Android will install them.

Editing an APK invalidates its original signature. ShiftDex signs the
result with apksigner when the Android SDK provides it, and falls back to
uber-apk-signer (downloaded on request) otherwise.
"""

import os
from pathlib import Path

from . import config, tools
from .runner import JobError

_AUTO_ALIAS = "shiftdex"
_AUTO_PASS = "shiftdex"


def auto_keystore_path():
    return config.keys_dir() / "shiftdex.jks"


def keystore(settings, ctx=None):
    """Keystore to sign with: the user's own, or a generated one.

    Returns {'path', 'alias', 'store_pass', 'key_pass'} or None when no
    keystore can be provided (keytool missing).
    """
    custom = (settings.get("keystore_path") or "").strip()
    if custom:
        if not os.path.isfile(custom):
            raise JobError(f"Keystore not found: {custom}")
        store_pass = settings.get("keystore_pass")
        return {
            "path": custom,
            "alias": settings.get("keystore_alias").strip(),
            "store_pass": store_pass,
            "key_pass": settings.get("key_pass") or store_pass,
        }
    path = auto_keystore_path()
    if not path.is_file():
        keytool = tools.find_java_tool("keytool", settings)
        if not keytool:
            return None
        path.parent.mkdir(parents=True, exist_ok=True)
        if ctx:
            ctx.log("Creating a signing key for ShiftDex builds …")
        cmd = [
            keytool, "-genkeypair", "-keystore", path, "-storetype", "PKCS12",
            "-storepass", _AUTO_PASS, "-keypass", _AUTO_PASS, "-alias", _AUTO_ALIAS,
            "-keyalg", "RSA", "-keysize", "2048", "-validity", "10950",
            "-dname", "CN=ShiftDex, O=ShiftDex",
        ]
        if ctx:
            ctx.run(cmd, quiet=True)
        else:
            import subprocess

            subprocess.run([str(c) for c in cmd], check=True, capture_output=True)
        os.chmod(path, 0o600)
    return {"path": str(path), "alias": _AUTO_ALIAS, "store_pass": _AUTO_PASS, "key_pass": _AUTO_PASS}


def sign(apk, settings, ctx):
    """Sign apk in place. Returns True when signed, False when no signer exists."""
    apk = Path(apk)
    apksigner = tools.find_apksigner()
    uber = tools.uber_signer()
    if not apksigner and not uber:
        ctx.log("No APK signer is available, so the APK was left unsigned.")
        ctx.log("Android will refuse to install it. Download the signer in Preferences → Tools.")
        return False

    ks = keystore(settings, ctx)
    if apksigner and ks:
        ctx.log("Signing with apksigner …")
        env = dict(os.environ, SHIFTDEX_KS_PASS=ks["store_pass"], SHIFTDEX_KEY_PASS=ks["key_pass"])
        cmd = [apksigner, "sign", "--ks", ks["path"], "--ks-pass", "env:SHIFTDEX_KS_PASS",
               "--key-pass", "env:SHIFTDEX_KEY_PASS"]
        if ks["alias"]:
            cmd += ["--ks-key-alias", ks["alias"]]
        cmd.append(apk)
        output = ctx.run(cmd, env=env, quiet=True)
        for line in output.splitlines():
            # the JDK prints native-access notices for apksigner; not useful here
            if line.strip() and not line.startswith("WARNING:"):
                ctx.log(line)
        Path(str(apk) + ".idsig").unlink(missing_ok=True)
        return True

    if uber:
        ctx.log("Signing with uber-apk-signer …")
        cmd = tools.java_command(settings) + ["-jar", uber["path"], "-a", apk, "--overwrite"]
        display = f"java -jar {uber['path'].name} -a {apk.name} --overwrite"
        if ks:
            cmd += ["--ks", ks["path"], "--ksPass", ks["store_pass"], "--ksKeyPass", ks["key_pass"]]
            if ks["alias"]:
                cmd += ["--ksAlias", ks["alias"]]
            display += " --ks " + Path(ks["path"]).name
        output = ctx.run(cmd, quiet=True, display=display)
        for line in output.splitlines():
            text = line.strip()
            if text.startswith(("- ", "Successfully", "Subject", "SHA256")):
                ctx.log(text)
        return True

    raise JobError("apksigner needs a keystore, but keytool was not found to create one.")
