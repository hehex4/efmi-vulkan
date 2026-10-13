"""Upload actual static-package DDS files through both DLL entries in headless children."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--package", type=Path, required=True)
    ap.add_argument("--dist", type=Path, default=ROOT / "build/next")
    ap.add_argument("--actual-config", action="store_true", help="read the DLL's actual settings instead of package environment overrides")
    ap.add_argument("--graphics-queues", type=int, default=1, help="enabled queues in the graphics family")
    ap.add_argument("--queue-index", type=int, default=0, help="only this queue submits; other enabled queues stay unused")
    args = ap.parse_args()
    if not 1 <= args.graphics_queues <= 32 or not 0 <= args.queue_index < args.graphics_queues:
        ap.error("queue index must be inside a 1..32 graphics queue count")
    package, dist = args.package.resolve(), args.dist.resolve()
    metadata = json.loads(package.with_suffix(".json").read_text())
    if hashlib.sha256(package.read_bytes()).hexdigest() != metadata["package_sha256"]:
        raise ValueError("Character package changed")
    for file in metadata["materials"]["files"]:
        if hashlib.sha256(Path(file["path"]).read_bytes()).hexdigest() != file["sha256"]:
            raise ValueError("Character texture changed since packaging")
    os.environ["EFMI_VK_DIST"] = str(dist)
    sys.path.insert(0, str(ROOT / "sandbox"))
    from harness import layer_env, inject_env, test_dir
    report = dict(scope="Actual files uploaded in headless sandbox children; no character/game rendering", passed=False,
                  package=str(package), package_sha256=metadata["package_sha256"], runs=[],
                  graphics_queues=args.graphics_queues, queue_index=args.queue_index)
    if args.actual_config:
        report["config"] = dict(path=str(dist / "efmi_vk.cfg"),
                                sha256=hashlib.sha256((dist / "efmi_vk.cfg").read_bytes()).hexdigest())
    out = ROOT / "build/character-assets"
    out.mkdir(parents=True, exist_ok=True)
    expected = len(metadata["materials"]["files"])
    try:
        for entry in ("layer", "inject"):
            folder = Path(test_dir("character-assets-"))
            env = (layer_env if entry == "layer" else inject_env)(str(folder / "layer.log"))
            validation = "F:/vulkan/validation-1.4.321.0/bin"
            env.update(VK_LAYER_PATH=os.pathsep.join([str(dist), validation]), VK_LAYER_VALIDATE_SYNC="1",
                       VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0", XDG_CACHE_HOME=str(folder), TMPDIR=str(folder),
                       TEMP=str(folder), TMP=str(folder), EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1")
            if args.actual_config:
                for key in list(env):
                    if key.startswith("EFMI_VK_") and key != "EFMI_VK_LOG":
                        del env[key]
            command = [str(dist / "efmi_vk_variant_gpu.exe"), "--character-assets", str(expected),
                       "--graphics-queues", str(args.graphics_queues), "--queue-index", str(args.queue_index)]
            if entry == "inject":
                command += ["--inject", str(dist / "efmi_vk_inject.dll")]
            proc = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, timeout=45)
            (folder / "stdout.txt").write_text(proc.stdout + proc.stderr)
            if proc.returncode or "validation: errors=0 warnings=0" not in proc.stdout:
                raise ValueError(f"{entry} asset upload failed: {folder}")
            status = json.loads((folder / "character-assets.json").read_text())["result"]
            r = status["replacement"]
            if args.actual_config and (status["mode"] != "probe" or not status["resources"] or
                    status["counts"]["image_content"]["enabled"] or status["tracker_work"]["enabled"]):
                raise ValueError("Actual material probe configuration disagrees")
            if not all(r[key] for key in ("configured", "enabled", "ready", "materials")) or r["images_failed"] or r["matched"]:
                raise ValueError(f"{entry} asset status disagrees: {folder}")
            if [r[key] for key in ("image_file_reads", "image_submissions", "images_ready")] != [expected] * 3:
                raise ValueError("Character textures were not read/submitted exactly once")
            if not r["images_published"] or r["image_queue_waits"] != 1 or r["material_graphics_queues"] != args.graphics_queues or r["image_upload_queue_index"] != args.queue_index:
                raise ValueError("Character material queue publication disagrees")
            report["runs"].append(dict(entry=entry, folder=str(folder), version=status["version"], replacement=r))
        report["passed"] = True
        if args.actual_config and hashlib.sha256((dist / "efmi_vk.cfg").read_bytes()).hexdigest() != report["config"]["sha256"]:
            report["passed"] = False
            raise ValueError("Configuration changed during asset smoke")
        print(json.dumps(dict(passed=True, textures=expected, runs=report["runs"]), indent=1))
        return 0
    finally:
        (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
