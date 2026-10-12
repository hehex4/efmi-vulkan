"""Persist one probe observation for later offline analysis; never starts a target process."""

from __future__ import annotations

from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

from .hashes import crc32c, format_hash
from .mods import find_ini_files, match_overrides, parse_overrides
from .probe_client import ProbeClient, ProbeError
from .spirv import inspect_spirv

ROOT = Path(__file__).resolve().parents[2]


def collect(pid: int, target: str, output: Path, mod: Path | None = None, pipeline_limit: int = 16,
            snapshot: dict | None = None, *, capture_delay_ms: int = 0) -> dict:
    """Capture a raw frame, counters, optional INI matches and bounded pipeline/shader metadata.

    A manifest is written last; an interrupted run retains its earlier files without
    pretending to be complete. Missing live pipeline/shader objects are recorded as gaps.

    ``snapshot`` is the request of :meth:`ProbeClient.capture` for buffer contents. Without
    "hashes" or "all" in it, the hashes of the mod's TextureOverride sections select the draws.
    The kept bytes go into ``snapshots.bin``, described by ``snapshots.json``.
    """
    if target not in ("sandbox", "game") or pid <= 0 or not 0 <= pipeline_limit <= 128:
        raise ValueError("explicit target, positive PID and pipeline limit 0..128 are required")
    if not 0 <= capture_delay_ms <= 1000:
        raise ValueError("capture_delay_ms must be between 0 and 1000")
    output = output.resolve()
    output.relative_to(ROOT)
    if mod is not None and not mod.exists():
        raise ValueError("mod path does not exist")
    files = {}

    def save(name: str, data):
        payload = data if isinstance(data, bytes) else (json.dumps(data, indent=2, ensure_ascii=True) + "\n").encode("ascii")
        (output / name).write_bytes(payload)
        files[name] = {"bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}

    with ProbeClient(pid) as probe:
        before = probe.status()
        wanted_exe = "efmi_vk_sandbox.exe" if target == "sandbox" else "endfield.exe"
        if before.get("exe", "").lower() != wanted_exe:
            raise ValueError(f"target label {target} disagrees with process {before.get('exe')}")
        if before.get("mode") != "probe":
            raise ValueError("target must already be running in probe mode")
        output.mkdir(parents=True, exist_ok=False)
        observed = datetime.now(timezone.utc).isoformat()
        save("status-before.json", before)
        save("api-before.json", probe.api_usage())
        request = None
        if snapshot is not None:
            request = dict(snapshot)
            if not request.get("all") and not request.get("hashes"):
                if mod is None:
                    raise ValueError("a snapshot needs hashes, all draws, or a mod to take the hashes from")
                request["hashes"] = sorted({o["hash"] for name in find_ini_files(str(mod)) for o in parse_overrides(name)})
        # Wait after the status/API queries, whose response timing can otherwise align repeated
        # requests with the same non-graphics present interval. Each result is still one interval.
        if capture_delay_ms:
            time.sleep(capture_delay_ms / 1000)
        frame = probe.capture(snapshot=request)
        blobs, missing = [], []
        if request is not None:
            # Fetched first: the layer keeps the bytes only while this capture is its latest one.
            packed = bytearray()
            for blob_id in range(frame["snapshot"]["blobs"]):
                try:
                    meta, data = probe.snapshot(frame["seq"], blob_id)
                    if len(data) != meta["size"] or format_hash(crc32c(data)) != meta["crc"]:
                        raise ValueError("snapshot bytes disagree with probe metadata")
                    blobs.append(dict(meta, file_offset=len(packed)))
                    packed += data
                except (ProbeError, ValueError) as exc:
                    missing.append({"snapshot": blob_id, "reason": str(exc)})
            save("snapshots.bin", bytes(packed))
            save("snapshots.json", {"request": request, "blobs": blobs})
        save("frame.json", frame)
        draws = frame["draws"]
        matched_draws = set()
        matches = []
        if mod is not None:
            for name in find_ini_files(str(mod)):
                path = Path(name)
                results = match_overrides(parse_overrides(name), draws)
                matches.append({"ini": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                "sections": results})
                for section in results:
                    matched_draws.update(section["draws"])
            save("matches.json", matches)
        # Prefer the sample's pipelines, then other frame pipelines. Every omission is explicit.
        prioritized = [d for d in draws if d["n"] in matched_draws] + [d for d in draws if d["n"] not in matched_draws]
        ids = list(dict.fromkeys(int(d["pipeline"]) for d in prioritized if d.get("pipeline")))
        selected = ids[:pipeline_limit]
        pipelines, shaders, gaps = [], {}, list(missing)
        for pipeline_id in selected:
            try:
                info = probe.pipeline(pipeline_id)
                pipelines.append(info)
            except ProbeError as exc:
                gaps.append({"pipeline": pipeline_id, "reason": str(exc)})
                continue
            for stage in info.get("shaders", []):
                if "id" not in stage:
                    gaps.append({"pipeline": pipeline_id, "stage": stage.get("stage"),
                                 "reason": "shader module was not seen by the probe"})
                    continue
                shader_id = int(stage["id"])
                if shader_id in shaders:
                    continue
                try:
                    meta, code = probe.shader(shader_id)
                    if len(code) != meta["size"] or format_hash(crc32c(code)) != meta["hash"]:
                        raise ValueError("shader bytes disagree with probe metadata")
                    name = f"shader-{shader_id}.spv"
                    save(name, code)
                    try:
                        reflection = inspect_spirv(code)
                    except ValueError as exc:
                        reflection = {"error": str(exc)}
                    shaders[shader_id] = dict(meta, file=name, declared_spirv=reflection)
                except (ProbeError, ValueError) as exc:
                    shaders[shader_id] = {"id": shader_id, "error": str(exc)}
                    gaps.append({"shader": shader_id, "reason": str(exc)})
        save("pipelines.json", pipelines)
        save("shaders.json", list(shaders.values()))
        save("api-after.json", probe.api_usage())
        save("status-after.json", probe.status())
        summary = {
            "capture_delay_ms": capture_delay_ms,
            "frame": frame["frame"], "draw_count": frame["draw_count"], "stats": frame["stats"],
            "draws_by_thread": dict(Counter(str(d["tid"]) for d in draws)),
            "draws_by_command_buffer": dict(Counter(str(d["cb"]) for d in draws)),
            "draws_by_render_pass": dict(Counter(str(d["rp"]) for d in draws)),
            "draws_by_kind": dict(Counter(d["kind"] for d in draws)),
            "hash_sources": dict(Counter(d["ib"]["src"] for d in draws if d.get("ib"))),
            "mod_match_requested": mod is not None, "matched_draws": sorted(matched_draws),
            "matching_rules": sum(s["matched"] for m in matches for s in m["sections"]),
            "queried_pipelines": selected, "omitted_pipelines": ids[pipeline_limit:], "query_gaps": gaps,
            "descriptor_bytes": frame.get("descriptor_bytes"), "descriptors_omitted": frame.get("descriptors_omitted"),
            "snapshot": frame.get("snapshot"),
        }
        save("summary.json", summary)
    manifest = {
        "schema": 1, "collection": "complete_with_gaps" if gaps or ids[pipeline_limit:] else "complete",
        "source": {"kind": target, "pid": pid, "exe": before["exe"], "layer_version": before["version"]},
        "observed_utc": observed, "files": files,
        "limits": ["One observed frame; no stability or rendering-compatibility verdict.",
                   "Sample visibility is not checked by the collector.",
                   "Buffer contents were kept at submit for the selected draws and compared with a read at record time; "
                   "nothing is known about the bytes the GPU saw later." if snapshot is not None else
                   "Resource visibility is at submit; record-time byte availability remains unverified.",
                   "Pipeline/shader queries happen after capture and may fail if objects have died.",
                   "Requests are not an atomic multi-device snapshot.",
                   "SPIR-V inspection lists declarations only; no full reflection or DX11 slot mapping."],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    return manifest
