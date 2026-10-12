"""Bounded capture selection; preserve every attempt and never infer a scene from a filename."""

from __future__ import annotations

from fnmatch import fnmatchcase
import hashlib
import json
from pathlib import Path

from .evidence import ROOT, collect
from .mods import find_ini_files, parse_overrides
from .probe_client import ProbeError


def _matches(name: str, pattern: str) -> bool:
    return fnmatchcase(name.casefold(), pattern.casefold())


def _has_color(pipeline: dict | None) -> bool | None:
    """Whether the selected subpass has a used color attachment; unknown is not a success."""
    recipe = (pipeline or {}).get("recipe", {})
    render_pass = recipe.get("render_pass", {})
    subpass = recipe.get("subpass")
    subpasses = render_pass.get("subpasses", [])
    if not recipe.get("complete") or not render_pass.get("complete"):
        return None
    if not isinstance(subpass, int) or not 0 <= subpass < len(subpasses):
        return None
    colors = subpasses[subpass].get("colors")
    if colors is None:
        return None
    used = [r.get("attachment") for r in colors if r.get("attachment") != 0xFFFFFFFF]
    if any(not isinstance(i, int) or not 0 <= i < len(render_pass.get("attachments", [])) for i in used):
        return None
    return bool(used)


def evaluate_selection(frame: dict, matches: list, pipelines: list, section_pattern: str,
                       require_color: bool = False) -> dict:
    """Test mesh-section presence, optionally in a color subpass, not on-screen visibility."""
    sections = [s for mod in matches for s in mod["sections"] if _matches(s["section"], section_pattern)]
    wanted = {n for s in sections for n in s["draws"]}
    selected = [d for d in frame["draws"] if d["n"] in wanted]
    by_id = {p["id"]: p for p in pipelines}
    classified = {d["n"]: _has_color(by_id.get(d["pipeline"])) for d in selected}
    color = [n for n, state in classified.items() if state is True]
    return {
        "satisfied": bool(color if require_color else selected),
        "matched_sections": [s["section"] for s in sections if s["draws"]],
        "matched_draws": [d["n"] for d in selected],
        "color_draws": color,
        "non_color_draws": [n for n, state in classified.items() if state is False],
        "unknown_draws": [n for n, state in classified.items() if state is None],
    }


def collect_selected(pid: int, target: str, output: Path, mod: Path, pipeline_limit: int = 128,
                     snapshot: dict | None = None, *, section_pattern: str, require_color: bool = False,
                     max_attempts: int = 8, wait_draws: bool = False) -> dict:
    """Stop at the first qualifying bundle or the attempt limit, retaining empty/rejected bundles.

    Each attempt has its own ordinary collection manifest and snapshot bytes, saved before the
    next capture can replace the layer's snapshot. series.json fingerprints those manifests and
    stays explicitly incomplete if interrupted. This never starts or reconfigures a process.
    """
    if not 1 <= max_attempts <= 16:
        raise ValueError("max_attempts must be between 1 and 16")
    if not section_pattern or mod is None or not mod.exists():
        raise ValueError("a mod and a nonempty section pattern are required")
    if not any(_matches(s["section"], section_pattern)
               for name in find_ini_files(str(mod)) for s in parse_overrides(name)):
        raise ValueError("section pattern matches no override in the mod")
    if target not in ("sandbox", "game") or pid <= 0 or not 0 <= pipeline_limit <= 128:
        raise ValueError("explicit target, positive PID and pipeline limit 0..128 are required")
    output = output.resolve()
    output.relative_to(ROOT)
    output.mkdir(parents=True, exist_ok=False)
    result = {
        "schema": 1, "selection": "collecting", "selected_bundle": None,
        "request": {"pid": pid, "target": target, "section_pattern": section_pattern,
                    "require_color": require_color, "max_attempts": max_attempts, "wait_draws": wait_draws},
        "attempts": [],
        "limits": ["Selection checks captured mesh sections, not user scene or visible pixels.",
                   "A color attachment does not prove the draw is the main visible pass.",
                   "Depth-only matches do not establish a distant LOD's color-pass coverage.",
                   "An empty present interval does not establish that the scene has no draws.",
                   "Selection success does not imply complete recipes, snapshots or compatibility."],
    }

    def save():
        (output / "series.json").write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="ascii")

    save()
    for number in range(1, max_attempts + 1):
        # Different short delays keep retries from following a fixed present cadence. A scene
        # may submit graphics only between some presents; do not merge those intervals.
        wait_ms = 0 if number == 1 else (13, 29, 47, 71, 19, 37, 59)[(number - 2) % 7]
        name = f"attempt-{number:02d}"
        bundle = output / name
        try:
            manifest = collect(pid, target, bundle, mod, pipeline_limit, snapshot,
                               capture_delay_ms=wait_ms, wait_draws=wait_draws)
        except (OSError, ValueError, ProbeError) as exc:
            result["selection"] = "interrupted"
            result["attempts"].append({"bundle": name, "error": str(exc)})
            save()
            raise
        frame = json.loads((bundle / "frame.json").read_text())
        check = evaluate_selection(frame, json.loads((bundle / "matches.json").read_text()),
                                   json.loads((bundle / "pipelines.json").read_text()),
                                   section_pattern, require_color)
        result["attempts"].append({
            "bundle": name, "frame": frame["frame"], "draw_count": frame["draw_count"],
            "wait_ms": wait_ms,
            "skipped_empty_intervals": frame.get("skipped_empty_intervals", 0),
            "collection": manifest["collection"], "source": manifest["source"],
            "manifest_sha256": hashlib.sha256((bundle / "manifest.json").read_bytes()).hexdigest(),
            **check,
        })
        if check["satisfied"]:
            result["selected_bundle"] = name
            result["selection"] = "met"
        elif number == max_attempts:
            result["selection"] = "exhausted"
        save()
        if check["satisfied"]:
            break
    return result
