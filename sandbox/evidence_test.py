"""Persist real sandbox observations and check evidence labels and query failure retention."""

import hashlib
import copy
import json
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

from harness import Checks, ROOT, Sandbox
from efmi_vk.evidence import collect
from efmi_vk.hashes import crc32c, format_hash
from efmi_vk.probe_client import ProbeClient, ProbeError
from efmi_vk.selection import collect_selected, evaluate_selection


def test_selection(c: Checks) -> None:
    frame = {"draws": [{"n": 0, "pipeline": 10}, {"n": 1, "pipeline": 11}]}
    matches = [{"sections": [{"section": "TextureOverrideMain", "draws": [0]},
                              {"section": "TextureOverrideMain_LOD1", "draws": [1]}]}]
    color = {"id": 10, "recipe": {"complete": True, "subpass": 0,
             "render_pass": {"complete": True, "attachments": [{}],
                             "subpasses": [{"colors": [{"attachment": 0}]}]}}}
    depth = copy.deepcopy(color)
    depth["id"] = 11
    depth["recipe"]["render_pass"]["subpasses"][0]["colors"] = []

    def evaluate(pipelines, color_required=True, candidate=frame):
        return evaluate_selection(candidate, matches, pipelines, "*_lod1", color_required)

    c.check(not evaluate([color, depth])["satisfied"], "LOD0 color plus LOD1 depth is not LOD1 color")
    c.equal(evaluate([color, depth])["non_color_draws"], [1], "depth-only LOD1 remains explicit")
    c.check(evaluate([color, depth], False)["satisfied"], "section-only selection accepts depth coverage")
    c.check(not evaluate([color, depth], candidate={"draws": []})["satisfied"], "empty interval is not accepted")
    c.equal(evaluate([color])["unknown_draws"], [1], "missing pipeline is unknown, not color")
    unused = copy.deepcopy(depth)
    unused["recipe"]["render_pass"]["subpasses"][0]["colors"] = [{"attachment": 0xFFFFFFFF}]
    c.check(not evaluate([color, unused])["satisfied"], "unused color slot is not color output")
    multi = copy.deepcopy(depth)
    multi["recipe"]["render_pass"]["subpasses"].append({"colors": [{"attachment": 0}]})
    c.check(not evaluate([color, multi])["satisfied"], "another subpass's color does not qualify")
    multi["recipe"]["subpass"] = 1
    c.equal(evaluate([color, multi])["color_draws"], [1], "selected color subpass qualifies")
    multi["recipe"]["complete"] = False
    c.equal(evaluate([color, multi])["unknown_draws"], [1], "incomplete recipe cannot qualify")


def main() -> int:
    c = Checks("probe evidence")
    test_selection(c)
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
            # The same collection with the buffer contents of the matched draw.
            uni = box.expected["uniform"]
            kept = Path(box.dir) / "with-snapshot"
            rule = "0:0:%d:2:%d:%d:%d" % (uni["follow_at"], uni["follow_stride"], uni["follow_bytes"], uni["late_at"])
            base = [sys.executable, "-B", str(Path(ROOT) / "scripts/collect_probe.py"), "--pid", str(box.pid),
                    "--target", "sandbox", "--mod", str(ini)]
            result = subprocess.run(base + ["--out", str(kept), "--snapshot", "mod", "--follow", rule, "--wait-draws"],
                                    capture_output=True, text=True, timeout=20)
            c.equal(result.returncode, 0, "collector CLI with a snapshot completed")
            manifest = json.loads((kept / "manifest.json").read_text())
            c.equal(manifest["collection"], "complete", "snapshot collection has no gaps")
            kept_summary = json.loads((kept / "summary.json").read_text())
            c.check(kept_summary["wait_draws"] and kept_summary["skipped_empty_intervals"] == 0,
                    "CLI preserves nonempty capture selection metadata")
            c.check(any("compared with a read at record time" in text for text in manifest["limits"]),
                    "the manifest says what the snapshot covers")
            frame = json.loads((kept / "frame.json").read_text())
            index = json.loads((kept / "snapshots.json").read_text())
            packed = (kept / "snapshots.bin").read_bytes()
            c.equal(index["request"]["hashes"], [box.expected["draws"][0]["hash"]], "hashes come from the mod")
            c.equal(index["request"]["follow"]["entry"], uni["late_at"], "the follow rule is preserved")
            c.equal([i for i, d in enumerate(frame["draws"]) if "snapshot" in d], [0], "only the matched draw has contents")
            c.equal((len(index["blobs"]), sum(b["size"] for b in index["blobs"])), (frame["snapshot"]["blobs"], len(packed)),
                    "every kept range is in the packed file")
            c.check(all(format_hash(crc32c(packed[b["file_offset"]:b["file_offset"] + b["size"]])) == b["crc"] for b in index["blobs"]),
                    "packed bytes agree with their checksums")
            c.equal(sorted(s["id"] for s in frame["draws"][0]["snapshot"]), list(range(len(index["blobs"]))),
                    "the draw's items point at the packed ranges")
            c.equal(json.loads((kept / "summary.json").read_text())["snapshot"]["draws"], 1, "summary carries the snapshot totals")
            for name in ("snapshots.bin", "snapshots.json"):
                c.check(name in manifest["files"], "manifest lists " + name)
            refused = subprocess.run(base + ["--out", str(Path(box.dir) / "refused"), "--follow", rule],
                                     capture_output=True, text=True, timeout=20)
            c.check(refused.returncode != 0 and not (Path(box.dir) / "refused").exists(), "a follow rule without a snapshot is refused")
            with patch.object(ProbeClient, "pipeline", side_effect=ProbeError("simulated destroyed pipeline")):
                gap = Path(box.dir) / "with-gap"
                m = collect(box.pid, "sandbox", gap)
                c.equal(m["collection"], "complete_with_gaps", "post-capture lifetime gap is explicit")
                c.equal(len(json.loads((gap / "frame.json").read_text())["draws"]), 5, "gap does not discard raw frame")

            # Emulate a present interval without graphics, followed by an actual GPU sandbox frame.
            # Keep this synthetic first interval labelled as sandbox; it is not game evidence.
            original_capture = ProbeClient.capture
            count = 0

            def empty_first(client, *args, **kwargs):
                nonlocal count
                raw = original_capture(client, *args, **kwargs)
                count += 1
                if count == 1:
                    raw["draws"] = []
                    raw["draw_count"] = 0
                return raw

            selected = Path(box.dir) / "selected"
            with patch.object(ProbeClient, "capture", empty_first):
                series = collect_selected(box.pid, "sandbox", selected, ini,
                                          section_pattern="TextureOverrideTest", require_color=True, max_attempts=3)
            c.equal(series["selection"], "met", "bounded selector reaches the actual color frame")
            c.equal(len(series["attempts"]), 2, "selection stops after success")
            c.equal(series["selected_bundle"], "attempt-02", "selected bundle is explicit")
            c.equal(series["attempts"][0]["draw_count"], 0, "empty attempt retained")
            c.equal(json.loads((selected / "series.json").read_text()), series, "series persisted")
            for attempt in series["attempts"]:
                raw = (selected / attempt["bundle"] / "manifest.json").read_bytes()
                c.equal(hashlib.sha256(raw).hexdigest(), attempt["manifest_sha256"], "series fingerprints child manifest")

            missed_ini = Path(box.dir) / "no-match.ini"
            missed_ini.write_text("[TextureOverrideMissing]\nhash = deadbeef\n", encoding="ascii")
            exhausted = Path(box.dir) / "exhausted"
            result = subprocess.run([sys.executable, "-B", str(Path(ROOT) / "scripts/collect_probe.py"),
                                     "--pid", str(box.pid), "--target", "sandbox", "--out", str(exhausted),
                                     "--mod", str(missed_ini), "--require-section", "*Missing", "--require-color",
                                     "--attempts", "2"], capture_output=True, text=True, timeout=20)
            c.equal(result.returncode, 2, "CLI reports selection exhaustion, not success")
            series = json.loads((exhausted / "series.json").read_text())
            c.equal((series["selection"], len(series["attempts"]), series["selected_bundle"]),
                    ("exhausted", 2, None), "exhaustion preserves both attempts without a selected frame")
            invalid = Path(box.dir) / "invalid-selection"
            try:
                collect_selected(box.pid, "sandbox", invalid, ini, section_pattern="*Absent")
                c.check(False, "misspelled section pattern was accepted")
            except ValueError:
                c.check(not invalid.exists(), "unknown section pattern rejected before collection")
            interrupted = Path(box.dir) / "interrupted-selection"
            with patch.object(ProbeClient, "capture", side_effect=ProbeError("simulated disconnect")):
                try:
                    collect_selected(box.pid, "sandbox", interrupted, ini, section_pattern="*Test")
                    c.check(False, "capture error was hidden")
                except ProbeError:
                    saved = json.loads((interrupted / "series.json").read_text())
                    c.equal(saved["selection"], "interrupted", "disconnect cannot leave a successful series")
            with ProbeClient(box.pid) as probe, patch.object(ProbeClient, "status", return_value={"features": ["capture"]}):
                try:
                    probe.capture(wait_draws=True)
                    c.check(False, "old layer silently accepted a wait-draws request")
                except ProbeError:
                    c.check(True, "old layer is explicitly refused for wait-draws")
    return c.finish()


if __name__ == "__main__":
    raise SystemExit(main())
