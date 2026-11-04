#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Project-local discovery/restart adaptation and worker supervision.
"""Tangram's CLI adapter for fm1-base/tools/fm1_install.py (MPL-2.0).

The update protocol remains Charles Vestal and Leo Kuroshita's implementation
in base/tools/fm1_install.py. Only discovery, deadlines and restart verification
are extended here. No files in the submodule are changed.

Run with native Windows Python to access Windows USB-MIDI; both this script and
the package may reside on a WSL UNC share. See INSTALL.md alongside this file.
The upstream PACKAGE/--info/--yes/--force/--port arguments remain available;
normal installation discovers the device automatically without --port.
"""
import importlib.util
import json
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True
EVENT = "TANGRAM_INSTALL_STAGE "


def stage(name, seconds):
    """Control channel to the supervisor; user-facing progress goes to stderr."""
    print(EVENT + json.dumps([name, seconds]), flush=True)


def worker(argv):
    stage("loading the installer", 15)
    path = Path(__file__).resolve().parents[1] / "base" / "tools" / "fm1_install.py"
    if not path.is_file():
        print(f"error: missing {path}; initialize the fm1-base submodule first", file=sys.stderr)
        return 2
    spec = importlib.util.spec_from_file_location("tangram_base_installer", path)
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    upstream_updater = base.Updater
    base.PORT_RE = re.compile(r"fm-(?:berlinan|tangram)|" + base.PORT_RE.pattern, base.PORT_RE.flags)

    class ProjectUpdater(upstream_updater):
        deadline = None

        def find(self, want=None, only_port=True):
            if self.deadline is None:
                stage("discovering the FM-1", 15)
            end = self.deadline if self.deadline is not None else time.monotonic() + 10
            outs = self.backend.output_names()
            for name in self.candidates(only_port):
                if time.monotonic() >= end:
                    break
                out = base.pair_output(name, outs)
                if not out:
                    continue
                try:
                    link = self.backend.open(name, out)
                except Exception:
                    continue  # The old endpoint may vanish during re-enumeration.
                keep = False
                try:
                    for _ in range(2):
                        if time.monotonic() >= end:
                            break
                        ident = base.handshake(link, tries=1)
                        if ident and (want is None or want(ident)):
                            keep = True
                            return base.Device(link, ident, name)
                except Exception:
                    pass  # Continue with the next currently enumerated pair.
                finally:
                    if not keep:
                        link.close()
            return None

        def wait_for(self, want, secs):
            # Fresh MIDI enumeration on every pass; an old --port name is only
            # a preference here, never a requirement after a USB restart.
            stage("waiting for the FM-1 identity", secs + 2)
            self.deadline = time.monotonic() + secs
            try:
                while time.monotonic() < self.deadline:
                    dev = self.find(want, only_port=False)
                    if dev:
                        return dev
                    time.sleep(max(0, min(base.DELAY["retry"], self.deadline - time.monotonic())))
                return None
            finally:
                self.deadline = None

        def arm(self, dev, image, product, digest):
            stage("requesting update permission from the device", 15)
            return super().arm(dev, image, product, digest)

        def check(self, dev, image, step):
            stage("checking the package on the device", 60)
            return super().check(dev, image, step)

        def write(self, ota, image, step):
            # Upstream retains its 180-second idle timeout; this absolute limit
            # also bounds a stuck driver or endless repeated read requests.
            stage("writing firmware", 600)
            return super().write(ota, image, step)

        def verify(self, product, step):
            step("reboot")
            stage("waiting for firmware restart", base.DELAY["reboot"] + base.DELAY["wait_reboot"] + 2)
            time.sleep(base.DELAY["reboot"])
            reported = set()

            def expected(ident):
                if ident.loader:
                    return False
                if ident.text == product:
                    return True
                if base.model_of(ident.text) == base.model_of(product):
                    reported.add(ident.text)
                return False

            back = self.wait_for(expected, base.DELAY["wait_reboot"])
            if not back:
                if reported:
                    raise base.InstallError("mismatch", f"written, but firmware reports {', '.join(sorted(reported))}; "
                                            f"expected {product}. Reconnect the FM-1 and check with --info")
                raise base.InstallError("noreturn", f"no restart identity {product} within "
                                        f"{base.DELAY['wait_reboot']:g} seconds. Close other MIDI apps, reconnect "
                                        "the FM-1, then use --info; rerun the same package if it is in update mode")
            back.link.close()
            step("done", back.id.text)
            return back.id.text

    # The upstream CLI still loads/checks the package, confirms installation,
    # validates the model, supports loader recovery and enforces Open/V15 rules.
    base.Updater = ProjectUpdater

    def ask(prompt):
        stage("waiting for confirmation", None)
        print(prompt, end="", file=sys.stderr, flush=True)
        answer = sys.stdin.readline().strip().lower() in ("y", "yes")
        stage("finishing confirmation", 15)
        return answer

    try:
        return base.main(argv, out=sys.stderr, ask=ask)
    except Exception as error:
        print(f"error: {error}. Close other MIDI apps and reconnect the FM-1. "
              "If it is in update mode, rerun the same package to finish.", file=sys.stderr)
        return 1


def supervise(argv):
    # A thread cannot reliably interrupt a blocked native MIDI driver call.
    # Keep protocol work in one child and enforce its stage deadlines outside it.
    child = subprocess.Popen([sys.executable, "-u", str(Path(__file__).resolve()), "--_worker", *argv],
                             stdout=subprocess.PIPE, text=True, encoding="utf-8")
    events = queue.Queue()

    def read_events():
        for line in child.stdout:
            events.put(line)
        events.put(None)

    threading.Thread(target=read_events, daemon=True).start()
    name, deadline = "starting Python", time.monotonic() + 15
    try:
        while True:
            left = 1 if deadline is None else deadline - time.monotonic()
            if left <= 0:
                child.kill()
                child.wait()
                print(f"error: {name} timed out. Close other MIDI apps, reconnect the FM-1 and retry. "
                      "If it is in update mode, rerun the same package to finish. "
                      "Installation was not confirmed.", file=sys.stderr)
                return 5
            try:
                line = events.get(timeout=min(left, 1))
            except queue.Empty:
                continue
            if line is None:
                return child.wait(timeout=5)
            if line.startswith(EVENT):
                name, seconds = json.loads(line[len(EVENT):])
                deadline = None if seconds is None else time.monotonic() + seconds
            else:
                print(line, end="", file=sys.stderr)
    except KeyboardInterrupt:
        print("\ninterrupted: if the FM-1 is in update mode, rerun the same package to finish", file=sys.stderr)
        return 130
    except subprocess.TimeoutExpired:
        print("error: MIDI worker did not exit. Reconnect the FM-1 and check with --info; "
              "installation was not confirmed.", file=sys.stderr)
        return 5
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        child.stdout.close()


if __name__ == "__main__":
    if sys.argv[1:2] == ["--_worker"]:
        sys.exit(worker(sys.argv[2:]))
    sys.exit(supervise(sys.argv[1:]))
