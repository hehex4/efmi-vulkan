"""Persist real sandbox observations and check evidence labels and query failure retention."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

from harness import Checks, ROOT, Sandbox
from efmi_vk.evidence import collect
from efmi_vk.probe_client import ProbeClient, ProbeError


def main() -> int:
    c = Checks("probe evidence")
    for entry in ("layer", "inject"):
        with Sandbox(threads=2, entry=entry, descriptors="push-template") as box:
            dest = Path(box.dir) / "evidence"
            ini = Path(box.dir) / "fixture.ini"
            ini.write_text("[TextureOverrideTest]\nhash = " + box.expected["draws"][0]["hash"] + "\n", encoding="ascii")
            result = subprocess.run([sys.executable, "-B", str(Path(ROOT) / "scripts/collect_probe.py"),
                                     "--pid", str(box.pid), "--target", "sandbox", "--out", str(dest), "--mod", str(ini)],
                                    capture_output=True, text=True, timeout=20)
            c.equal(result.returncode, 0, "collector CLI completed")
            c.check("complete sandbox observation" in result.stdout, "CLI reports evidence scope")
            manifest = json.loads((dest / "manifest.json").read_text())
            c.equal(manifest["source"]["kind"], "sandbox", "sandbox evidence label")
            c.equal(manifest["source"]["pid"], box.pid, "explicit process identity")
            c.equal(manifest["collection"], "complete", "no missing queries")
            for name, expected in manifest["files"].items():
                data = (dest / name).read_bytes()
                c.equal((len(data), hashlib.sha256(data).hexdigest()),
                        (expected["bytes"], expected["sha256"]), "manifest verifies " + name)
            frame = json.loads((dest / "frame.json").read_text())
            summary = json.loads((dest / "summary.json").read_text())
            c.equal(len(frame["draws"]), 5, "raw frame preserved")
            c.equal(summary["matching_rules"], 1, "INI region match preserved")
            c.equal(len(summary["draws_by_thread"]), 2, "G2 recording threads preserved")
            c.equal(len(list(dest.glob("*.spv"))), 2, "both retained shaders saved")
            c.equal(frame["draws"][0]["sets"][1]["contents"]["snapshot"], "record", "push timing preserved")
            c.equal(frame["draws"][0]["sets"][1]["contents"]["resource_snapshot"], "submit", "resource timing preserved")
            for label, out in (("game", Path(box.dir) / "wrong-label"), ("sandbox", dest)):
                try:
                    collect(box.pid, label, out)
                    c.check(False, "wrong scope or overwritten evidence was allowed")
                except (ValueError, FileExistsError):
                    c.check(True, "wrong scope / overwrite refused")
            with patch.object(ProbeClient, "pipeline", side_effect=ProbeError("simulated destroyed pipeline")):
                gap = Path(box.dir) / "with-gap"
                m = collect(box.pid, "sandbox", gap)
                c.equal(m["collection"], "complete_with_gaps", "post-capture lifetime gap is explicit")
                c.equal(len(json.loads((gap / "frame.json").read_text())["draws"]), 5, "gap does not discard raw frame")
    return c.finish()


if __name__ == "__main__":
    raise SystemExit(main())
