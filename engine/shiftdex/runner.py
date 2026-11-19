# SPDX-License-Identifier: GPL-3.0-or-later
"""Run external tools and long tasks off the UI thread."""

import subprocess
import threading


class JobError(Exception):
    """A task failed in a way worth showing to the user as-is."""


class Cancelled(Exception):
    pass


class JobContext:
    """Handed to every task: a log sink and a cancel flag."""

    def __init__(self, on_log=None):
        self._on_log = on_log
        # set by the caller to receive a 0..1 fraction; ignored by default
        self.progress = lambda _fraction: None
        self._cancel = threading.Event()
        self._proc = None
        self._lock = threading.Lock()

    def log(self, text):
        if self._on_log:
            self._on_log(str(text))

    @property
    def cancelled(self):
        return self._cancel.is_set()

    def check(self):
        if self._cancel.is_set():
            raise Cancelled()

    def cancel(self):
        self._cancel.set()
        with self._lock:
            proc = self._proc
        if proc and proc.poll() is None:
            try:
                proc.terminate()
            except OSError:
                pass

    def run(self, cmd, cwd=None, quiet=False, env=None, display=None, on_line=None):
        """Run a command, streaming its output to the log.

        `display` replaces the logged command line (used to keep passwords
        out of the log). Returns the collected output. Raises JobError on a
        non-zero exit. `on_line(line)` sees every output line first and may
        return True to keep it out of the log (progress tickers).
        """
        self.check()
        cmd = [str(part) for part in cmd]
        if not quiet:
            self.log("$ " + (display or " ".join(_shorten(part) for part in cmd)))
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
        except OSError as exc:
            raise JobError(f"Could not start {cmd[0]}: {exc.strerror or exc}") from exc
        with self._lock:
            self._proc = proc
        lines = []
        try:
            for line in proc.stdout:
                line = line.rstrip("\n")
                if on_line and on_line(line):
                    continue
                lines.append(line)
                if not quiet:
                    self.log(line)
            code = proc.wait()
        finally:
            proc.stdout.close()
            with self._lock:
                self._proc = None
        if self._cancel.is_set():
            raise Cancelled()
        if code != 0:
            tail = _last_meaningful(lines)
            raise JobError(f"{_shorten(cmd[0])} exited with code {code}" + (f": {tail}" if tail else ""))
        return "\n".join(lines)


def _shorten(part):
    # keep log lines readable: show file names for long absolute paths
    if part.startswith("/") and len(part) > 48:
        return "…/" + part.rsplit("/", 1)[-1]
    return part


def _last_meaningful(lines):
    for line in reversed(lines):
        text = line.strip()
        if text and not text.startswith("WARNING:"):
            return text[:300]
    return ""


class Job:
    """Runs func(ctx) on a worker thread.

    `dispatch` marshals callbacks back to the UI thread (GLib.idle_add in
    the app, a plain call in tests).
    """

    def __init__(self, func, on_log=None, on_done=None, dispatch=None):
        self._func = func
        self._on_done = on_done
        self._dispatch = dispatch or (lambda fn, *args: fn(*args))
        self.ctx = JobContext((lambda text: self._dispatch(on_log, text)) if on_log else None)
        self._thread = threading.Thread(target=self._main, daemon=True)

    def start(self):
        self._thread.start()
        return self

    def cancel(self):
        self.ctx.cancel()

    def join(self, timeout=None):
        self._thread.join(timeout)

    def _main(self):
        result, error = None, None
        try:
            result = self._func(self.ctx)
        except Cancelled:
            error = Cancelled("Cancelled")
        except JobError as exc:
            error = exc
        except Exception as exc:  # noqa: BLE001 - reported to the user, not swallowed
            import traceback

            self.ctx.log(traceback.format_exc())
            error = JobError(f"{type(exc).__name__}: {exc}")
        if self._on_done:
            self._dispatch(self._on_done, result, error)
