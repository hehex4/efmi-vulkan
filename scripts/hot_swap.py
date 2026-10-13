"""Exchange the replacement code of a running process (docs/hot-swap.md).

Builds the replacement module from the working tree, copies it to a name of its own under
build/modules/ (a loaded DLL cannot be overwritten, and the layer never unloads one), asks the
layer in the process to take it, waits until it is in use, and appends what happened to
build/hot-swaps.json. Nothing else of the process is touched; hooks and tracker stay as loaded.

A module can only replace code behind the boundary of layer/src/character_module.h. If the shared
headers changed since the running layer was built, the stamps differ and this script says so
before building: that change needs the layer itself replaced, with the process closed.
"""
from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
sys.path.insert(0, str(ROOT / "scripts"))
from efmi_vk.probe_client import ProbeClient, ProbeError  # noqa: E402
import build as builder  # noqa: E402


def processes(image: str) -> list[int]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + image, "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, errors="replace").stdout
    return [int(row[1]) for row in csv.reader(io.StringIO(out)) if len(row) > 1 and row[0].lower() == image.lower()]


def source_state() -> dict:
    def git(*args):
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return {"commit": git("rev-parse", "HEAD"), "dirty": bool(git("status", "--porcelain", "--", "layer", "frontend"))}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pid", type=int, help="process to act on (default: the one process of --target)")
    ap.add_argument("--target", default="Endfield.exe", help="image name looked for when --pid is not given")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "next", help="where the module is built")
    ap.add_argument("--no-build", action="store_true", help="use the module that is already in --out")
    ap.add_argument("--package", help="character package for the new backend (default: the configured one)")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds to wait for the new backend to take over")
    ap.add_argument("--note", default="", help="what this exchange is for; kept in the record")
    args = ap.parse_args()

    pid = args.pid
    if pid is None:
        found = processes(args.target)
        if len(found) != 1:
            print("expected one %s process, found %d" % (args.target, len(found)))
            return 2
        pid = found[0]

    with ProbeClient(pid) as client:
        before = client.status()
    state = (before.get("replacement") or {}).get("module")
    if not state:
        print("layer %s in process %d cannot exchange its replacement code (needs 1.0.1 or later, with a character package)"
              % (before.get("version"), pid))
        return 2
    stamp = builder.abi_stamp()[2:-3]
    if state.get("abi") != stamp:
        print("the shared headers changed since the running layer was built (layer %s, tree %s):" % (state.get("abi"), stamp))
        print("this change needs the layer replaced with the process closed; headers: %s" % ", ".join(builder.abi_headers()))
        return 3

    out = args.out.resolve()
    if not args.no_build:
        try:
            builder.build(["module"], str(out))
        except builder.BuildError as e:
            print(str(e))
            return 1
    module = out / builder.MODULE_DLL
    data = module.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    folder = ROOT / "build" / "modules"
    folder.mkdir(parents=True, exist_ok=True)
    stamp_time = datetime.datetime.now().astimezone().replace(microsecond=0)
    # A name of its own for every exchange: the same file may still be loaded from an earlier one.
    base = "efmi_vk_backend-%s-%s" % (stamp_time.strftime("%Y%m%d-%H%M%S"), digest[:8])
    copy, serial = folder / (base + ".dll"), 1
    while copy.exists():
        serial += 1
        copy = folder / ("%s-%d.dll" % (base, serial))
    shutil.copyfile(module, copy)
    pdb = ROOT / "layer" / "build" / "module" / "module.pdb"
    if pdb.is_file():
        shutil.copyfile(pdb, copy.with_suffix(".pdb"))

    record = {"time": stamp_time.isoformat(), "pid": pid, "target": args.target, "layer_version": before.get("version"),
              "module_sha256": digest, "module_path": str(copy), "package": args.package or "", "note": args.note,
              "generation_before": state.get("generation"), "source": source_state(), "taken": False}
    started = time.monotonic()
    after = None
    try:
        with ProbeClient(pid) as client:
            staged = client.swap_module(copy, args.package)
            record["module_version"] = staged.get("version")
            while time.monotonic() - started < args.timeout:
                time.sleep(0.1)
                after = client.status()
                now = after["replacement"]["module"]
                if now["generation"] > state["generation"] or (now.get("last_error") and not now.get("staged")):
                    break
    except ProbeError as e:
        record["error"] = str(e)
    if after is not None:
        now = after["replacement"]["module"]
        record.update(seconds=round(time.monotonic() - started, 2), generation_after=now["generation"],
                      taken=now["generation"] > state["generation"] and now.get("path", "").replace("\\", "/") == str(copy).replace("\\", "/"),
                      error=record.get("error") or now.get("last_error", ""))
    log = ROOT / "build" / "hot-swaps.json"
    history = json.loads(log.read_text(encoding="utf-8")) if log.is_file() else []
    history.append(record)
    log.write_text(json.dumps(history, indent=1) + "\n", encoding="utf-8")

    # Copies no process holds any more can go.
    for old in folder.glob("efmi_vk_backend-*"):
        if old.stem != copy.stem:
            try:
                old.unlink()
            except OSError:
                pass

    if record["taken"]:
        r = after["replacement"]
        print("process %d: replacement code %s (%s) in use after %.1f s, generation %d; backend ready=%s, images %s/%s"
              % (pid, record.get("module_version"), digest[:12], record["seconds"], record["generation_after"],
                 r.get("ready"), r.get("images_ready"), r.get("image_count")))
        return 0
    print("process %d: the module was not taken: %s" % (pid, record.get("error") or "no change within %.0f s" % args.timeout))
    return 1


if __name__ == "__main__":
    sys.exit(main())
