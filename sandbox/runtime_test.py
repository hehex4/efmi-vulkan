"""Keys, live INI conditions, persistence and transactional F10 through Vulkan hooks."""
import json
import time
import os
from pathlib import Path
import re
import subprocess
from harness import Checks, DIST, INJECT_DLL, Sandbox, inject_env, layer_env, test_dir
from runtime_fixture import CORE, fixture
from efmi_vk.probe_client import ProbeClient


def refused_package(checks):
    """BUG-026: a package whose Mod files went missing is refused, and the application runs on."""
    for entry in ("layer", "inject"):
        folder = Path(test_dir("runtime-refused-"))
        package = fixture(folder)
        config = json.loads(Path(str(package) + ".runtime.json").read_text(encoding="ascii"))
        Path(config["stream_0"]).unlink()
        extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500")
        status = {}
        with Sandbox(seconds=4, env_extra=extra, entry=entry) as sb:
            with ProbeClient(sb.pid) as probe:
                status = probe.status().get("replacement", {})
            sb.proc.wait(timeout=60)
            code = sb.proc.returncode
        log = Path(sb.log_path).read_text(encoding="utf-8", errors="replace") if sb.log_path and Path(sb.log_path).is_file() else ""
        label = f"{entry}, missing Mod file"
        checks.equal(code, 0, label + ": the application runs and exits normally (BUG-026)")
        checks.check("character package refused: Runtime resource missing" in log, label + ": the refusal and its reason are logged")
        checks.check(not status.get("ready") and not status.get("enabled"), label + ": the refused backend neither is ready nor enabled")


def outside_mesh(checks):
    """Another INI of the Mod folder draws its own mesh at an object the package has no part for (as
    zhuangfangyi's "fangyi ul.ini"): the original loads it with the character, the package keeps
    working, and that mesh stays with the game's draws."""
    folder = Path(test_dir("runtime-outside-"))
    package = fixture(folder)
    (folder / "other-index.buf").write_bytes(bytes([0, 0, 1, 0, 2, 0]))
    (folder / "other.ini").write_text("""[TextureOverride_OtherMesh]
hash = 0badf00d
match_first_index = 0
match_index_count = 3
handling = skip
ib = ResourceOtherIndex
drawindexed = 3,0,0
[ResourceOtherIndex]
type = Buffer
format = DXGI_FORMAT_R16_UINT
filename = other-index.buf
""", encoding="ascii")
    extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500")
    status = {}
    with Sandbox(seconds=4, env_extra=extra, entry="layer") as sb:
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=60)
        code = sb.proc.returncode
    log = Path(sb.log_path).read_text(encoding="utf-8", errors="replace") if sb.log_path and Path(sb.log_path).is_file() else ""
    label = "another INI's mesh outside the package"
    checks.equal(code, 0, label + ": the application runs and exits normally")
    checks.check("character package configured" in log and "character package refused" not in log, label + ": the package is configured")
    checks.check("1 referenced mesh file(s) outside the package" in log, label + ": the mesh outside the package is logged")
    checks.check(status.get("enabled") is True, label + ": the backend is enabled")


def shader_sections(checks):
    """A ShaderOverride with only hash and filter_index (perlica's hair sections) runs nothing and
    loads; one that acts on the game's draws (xaihi's handling = skip) needs the game shaders' DX11
    identity, which the layer cannot see: it stays inert, the log names it, and the package loads
    (D-089)."""
    cases = (("declaring", "filter_index = 3737\nallow_duplicate_hash = overrule\n", False),
             ("acting", "handling = skip\n", True))
    for name, body, named in cases:
        folder = Path(test_dir("runtime-shader-"))
        package = fixture(folder)
        (folder / "shaders.ini").write_text("[ShaderOverrideFixture]\nhash = 5d7710a77fb65f25\n" + body, encoding="ascii")
        extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500")
        status = {}
        with Sandbox(seconds=4, env_extra=extra, entry="layer") as sb:
            with ProbeClient(sb.pid) as probe:
                status = probe.status().get("replacement", {})
            sb.proc.wait(timeout=60)
            code = sb.proc.returncode
        log = Path(sb.log_path).read_text(encoding="utf-8", errors="replace") if sb.log_path and Path(sb.log_path).is_file() else ""
        label = f"{name} ShaderOverride in another INI of the Mod folder"
        checks.equal(code, 0, label + ": the application runs and exits normally")
        checks.check("character package configured" in log and status.get("enabled") is True, label + ": the package loads")
        inert = [line for line in log.lower().splitlines() if "fixture] acts on game shaders" in line and "inert" in line]
        checks.check(bool(inert) == named,
                     label + (": the log names it as inert" if named else ": nothing is named as inert"))


def save_cadence(checks):
    """A persistent variable changed every frame (a key panel's animation) is saved as the original
    saves it: every settings_auto_save_interval seconds (60) and when the program ends, not at each
    change (BUG-058: each save held the engine lock through a flush to disk)."""
    folder = Path(test_dir("runtime-save-"))
    package = fixture(folder)
    (folder / "persist.ini").write_text("[Constants]\nglobal persist $counter = 0\n[Present]\n$counter = $counter + 1\n", encoding="ascii")
    extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1")
    status = {}
    with Sandbox(seconds=4, env_extra=extra, entry="layer") as sb:
        time.sleep(2.5)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=60)
        code = sb.proc.returncode
    ini = status.get("ini", {})
    counter = ini.get("variables", {}).get("$\\persist.ini\\counter", 0)
    label = "a persistent variable changed every frame"
    checks.equal(code, 0, label + ": the application runs and exits normally")
    checks.check(counter > 30, label + ": it changed every frame", counter)
    checks.equal(ini.get("saves"), 0, label + ": nothing is saved before the interval passed")
    saved = [p for p in folder.rglob("*.ini") if p.parent.name == "settings"]
    values = [float(line.split("=")[1]) for p in saved for line in p.read_text().splitlines() if line.startswith("$\\persist.ini\\counter")]
    checks.check(bool(values) and max(values) >= counter, label + ": the program's end saved its latest value", (saved, values, counter))


def main():
    checks = Checks("live INI rendering")
    validation = os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin")
    for entry in ("layer", "inject"):
        for queues in (1, 2):
            folder = Path(test_dir("runtime-"))
            package = fixture(folder)
            extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500", EFMI_VK_TEST_KEYS=str(folder / "keys.txt"),
                         VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                         XDG_CACHE_HOME=str(folder), TEMP=str(folder), TMP=str(folder), TMPDIR=str(folder))
            env = (layer_env if entry == "layer" else inject_env)(str(folder / "layer.log"), extra=extra)
            env["VK_LAYER_PATH"] = extra["VK_LAYER_PATH"]
            args = [str(Path(DIST) / "efmi_vk_variant_gpu.exe"), "--character", "--material", "--validation",
                    "--module", str(Path(DIST) / "efmi_vk_backend.dll"), "--bad-module", str(Path(DIST) / "efmi_vk_backend_mismatch.dll")]
            if queues == 2:
                args += ["--graphics-queues", "2", "--split-material-queues"]
            if entry == "inject":
                args += ["--inject", INJECT_DLL]
            result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors="replace", timeout=120)
            output = result.stdout + result.stderr
            (folder / "stdout.txt").write_text(output.encode("ascii", "backslashreplace").decode(), encoding="ascii")
            label = f"{entry}, {queues} queue(s)"
            checks.equal(result.returncode, 0, label + " GPU keys, reload, rollback and retained-command checks")
            checks.check("validation: errors=0 warnings=0" in output, label + " clean core/sync validation including shutdown")
            report = folder / "runtime-final.json"
            checks.check(report.is_file(), label + " final status saved")
            if result.returncode:
                print(output[-10000:]); print("artifacts:", folder)
            if not report.is_file():
                continue
            status = json.loads(report.read_text())["result"]["replacement"]
            checks.equal((status["ini_generation"], status["ini_reloads"], status["ini_reload_rejected"], status["ini_reload_pending"], status["ini_reload_error"]),
                         (2, 2, 1, False, ""), label + " two accepted reloads, one with an original warning, and one refused")
            checks.equal(status["ini"]["diagnostics"], 0, label + " the last generation has no warning left")
            checks.equal(status["ini"]["variables"]["$\\runtime_fixture\\tint"], 1, label + " persistent variable survives F10")
            variables = status["ini"]["variables"]
            checks.equal((variables.get("$\\runtime_fixture\\texture_seen"), variables.get("$\\runtime_fixture\\texture_other")), (1, 0),
                         label + " Core's OverrideTextures finds the application texture by its original hash, and only that (D-077)")
            checks.check(status.get("texture_queries", 0) > 0, label + " the layer answered texture queries")
            saved = folder / "settings/fixture.ini"
            checks.check(saved.is_file() and "$\\runtime_fixture\\tint = 1" in saved.read_text(), label + " settings atomically saved outside mod source")
            refused = {k: v for k, v in status["counters"].items() if k.startswith(("refused_", "unreadable_", "import_", "exhausted")) and v}
            checks.equal(refused, {}, label + " no refused drawing after successful reload")
    refused_package(checks)
    save_cadence(checks)
    outside_mesh(checks)
    shader_sections(checks)
    several_packages(checks, validation)
    directory_generation(checks, validation)
    implicit_namespace(checks)
    stride_branches(checks)
    lod_left_by_another_mod(checks)
    return checks.finish()


def several_packages(checks, validation):
    """A directory of packages: another character's package, which matches no draw of the scene,
    sorts first, so every draw passes it before the fixture's. The scene must behave as with one
    package, and the other package must never go to the GPU."""
    import shutil
    folder = Path(test_dir("runtime-several-"))
    (folder / "b").mkdir()
    fixture(folder / "b", salt=0x5A5A5A5A, settings="other", name="other.bin")
    package = fixture(folder)
    for suffix in ("", ".runtime.json"):
        shutil.copyfile(folder / "b" / ("other.bin" + suffix), folder / ("0-other.bin" + suffix))
    extra = dict(EFMI_VK_M2_PACKAGE=str(folder), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500", EFMI_VK_TEST_KEYS=str(folder / "keys.txt"),
                 VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                 XDG_CACHE_HOME=str(folder), TEMP=str(folder), TMP=str(folder), TMPDIR=str(folder))
    env = layer_env(str(folder / "layer.log"), extra=extra)
    env["VK_LAYER_PATH"] = extra["VK_LAYER_PATH"]
    args = [str(Path(DIST) / "efmi_vk_variant_gpu.exe"), "--character", "--material", "--validation", "--lazy-packages",
            "--module", str(Path(DIST) / "efmi_vk_backend.dll"), "--bad-module", str(Path(DIST) / "efmi_vk_backend_mismatch.dll")]
    result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors="replace", timeout=120)
    output = result.stdout + result.stderr
    (folder / "stdout.txt").write_text(output.encode("ascii", "backslashreplace").decode(), encoding="ascii")
    label = "two packages in a directory"
    checks.equal(result.returncode, 0, label + ": the scene's keys, reloads, exchanges and pixels as with one package")
    checks.check("validation: errors=0 warnings=0" in output, label + ": clean core/sync validation")
    if result.returncode:
        print(output[-6000:]); print("artifacts:", folder)
    report = folder / "runtime-final.json"
    status = json.loads(report.read_text())["result"]["replacement"] if report.is_file() else {}
    packages = status.get("packages", [])
    names = [Path(p.get("path", "")).name for p in packages]
    checks.equal(names, ["0-other.bin", "character.bin"], label + ": both packages listed in name order")
    held = [(p.get("package_bytes", 0) > 0, p.get("payload_held_bytes", -1)) for p in packages]
    checks.equal(held, [(True, 0)] * len(packages), label + ": no package holds its mesh bytes in host memory (ISSUE-011)")
    if len(packages) == 2:
        other, own = packages
        checks.equal((other["lazy"], other["activated"], other.get("images_ready", 0), other["counters"].get("replaced", 0)),
                     (True, False, 0, 0), label + ": the other character's package never matched and never went to the GPU")
        checks.check(own["lazy"] and own["activated"] and own["counters"].get("replaced", 0) > 0 and own["images_ready"] == 2,
                     label + ": the fixture's package went to the GPU on its first match and replaced draws")
        checks.equal((own["ini_generation"], own["ini_reloads"]), (2, 2), label + ": its reloads as with one package")
        log = (folder / "layer.log").read_text(encoding="utf-8", errors="replace")
        activations = [line for line in log.splitlines() if "activated on its first match" in line]
        checks.check(activations and all("character.bin" in line and "0-other.bin" not in line for line in activations),
                     label + ": only the fixture's package was activated, in each backend after the exchanges")


def implicit_namespace(checks):
    """An INI without a namespace line (D-078): the runtime parses it under its file name, which
    names its sections and variables as the original's implicit namespace does. The production
    planner (efmi_vk_character_test --plan-mods, no GPU) must read it and draw its components."""
    root = Path(test_dir("runtime-implicit-"))
    folder = root / "Mods" / "fixture"
    folder.mkdir(parents=True)
    fixture(folder)
    text = (folder / "mod.ini").read_text(encoding="ascii")
    first, rest = text.split("\n", 1)
    checks.equal(first, "namespace = runtime_fixture", "the fixture INI starts with its namespace line")
    (folder / "mod.ini").write_text(rest, encoding="ascii")
    run = subprocess.run([str(Path(DIST) / "efmi_vk_character_test.exe"), "--plan-mods", str(root / "Mods"), str(CORE),
                          str(root / "settings")], cwd=root, capture_output=True, text=True, errors="replace", timeout=120)
    rows = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
    label = "INI without a namespace line"
    checks.check(run.returncode == 0 and len(rows) == 1 and rows[0].get("loaded"), label + ": read under its file name " +
                 (rows[0].get("reason", "") if rows else run.stderr[-300:]))
    drawn = [s for s in (rows[0].get("sections", []) if rows else []) if s.get("planned") and s.get("skip") and s.get("draws")]
    checks.check(len(drawn) >= 2, label + ": its components are drawn by their live plans")


def stride_branch_ini(ini, branches):
    """The fixture's first TextureOverride chooses its component and LOD by `vb1->stride`, as an EFMI
    export may (LIMIT-041): `branches` is ((stride, component, lod), ...) in the order of the if/elif
    chain. Returns the section's hash."""
    text = ini.read_text(encoding="ascii")
    head, rest = text.split("[TextureOverride_EntryPoint_0]\n", 1)
    section, tail = rest.split("\n[", 1)
    hashed = re.search(r"^hash = (\w+)$", section, re.M).group(1)
    assignment = "$\\EFMIv1\\component_id = 0\n"
    assert assignment in section, "the fixture's first section assigns component 0"
    chain = "".join("%s vb1->stride == %d\n$lod_level = %d\n$\\EFMIv1\\component_id = %d\n" % ("if" if i == 0 else "elif", stride, lod, component)
                    for i, (stride, component, lod) in enumerate(branches)) + "endif\n"
    section = re.sub(r"^\$lod_level = \d+\n", "", section, flags=re.M).replace(assignment, chain)
    ini.write_text(head + "[TextureOverride_EntryPoint_0]\n" + section + "\n[" + tail, encoding="ascii")
    return hashed


def stride_branches(checks):
    """LIMIT-041: a TextureOverride that tells a component's LODs apart by `if vb1->stride == K`, as an
    EFMI export may (avywenna's face). The original reads the bound vertex buffer's stride per draw;
    discovery makes a section per branch, which the backend matches by the draw's pipeline binding
    and the production planner (efmi_vk_character_test --plan-mods, no GPU) plans with that stride."""
    root = Path(test_dir("runtime-stride-"))
    folder = root / "Mods" / "fixture"
    folder.mkdir(parents=True)
    fixture(folder)
    hashed = stride_branch_ini(folder / "mod.ini", ((12, 0, 0), (8, 1, 1)))
    run = subprocess.run([str(Path(DIST) / "efmi_vk_character_test.exe"), "--plan-mods", str(root / "Mods"), str(CORE),
                          str(root / "settings")], cwd=root, capture_output=True, text=True, errors="replace", timeout=120)
    rows = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
    label = "a component chosen by vb1->stride"
    checks.check(run.returncode == 0 and len(rows) == 1 and rows[0].get("loaded"), label + ": the Mod loads " +
                 (rows[0].get("reason", "") if rows else run.stderr[-300:]))
    found = [s for s in (rows[0].get("sections", []) if rows else []) if s.get("hash") == hashed]
    checks.equal([(s.get("component"), s.get("lod"), s.get("stride_slot"), s.get("stride")) for s in found],
                 [(0, 0, 1, 12), (1, 1, 1, 8)], label + ": a section per branch, with its component, LOD and stride")
    checks.check(len(found) == 2 and all(s.get("planned") and s.get("skip") and s.get("draws") for s in found),
                 label + ": each branch plans and draws with its stride " + str([s.get("reason", "") for s in found]))


def lod_left_by_another_mod(checks):
    """A Mod that never hands its LOD to the Core (Typhoeus: its LOD1 parts are components of their own)
    in the folder generation, after another Mod's draws left the Core's one `$\\EFMIv1\\lod_level` at 1,
    as in the original's one variable space. The Core reads that value only to pick the component's LOD
    remap (MergedSkeleton.ini, `LodRemaps[component * levels + lod_level]`): a component without remaps
    draws the same bones at any LOD and plans; one with a LOD1 remap must agree with the draw's LOD."""
    root = Path(test_dir("runtime-lod-"))
    mods = root / "Mods"
    (mods / "a").mkdir(parents=True)
    (mods / "b").mkdir()
    fixture(mods / "a", settings="a", name="a.bin", namespace="runtime_a")
    fixture(mods / "b", salt=0x5A5A5A5A, settings="b", name="b.bin", namespace="runtime_b")
    a = mods / "a" / "mod.ini"
    a.write_text(re.sub(r"^\$lod_level = \d+$", "$lod_level = 1", a.read_text(encoding="ascii"), flags=re.M), encoding="ascii")
    b = mods / "b" / "mod.ini"
    handed = "$\\EFMIv1\\lod_level = $lod_level\n"
    text = b.read_text(encoding="ascii")
    checks.check(handed in text, "the fixture hands its LOD to the Core")
    b.write_text(re.sub(r"^\$lod_level = \d+\n", "", text, flags=re.M).replace(handed, ""), encoding="ascii")
    run = subprocess.run([str(Path(DIST) / "efmi_vk_character_test.exe"), "--plan-folder", str(mods), str(CORE), str(root / "settings")],
                         cwd=root, capture_output=True, text=True, errors="replace", timeout=120)
    rows = {Path(r.get("ini", "")).parent.name: r for r in (json.loads(line) for line in run.stdout.splitlines() if line.startswith("{"))}
    label = "a Mod that leaves the Core's LOD to the others"
    checks.check(run.returncode == 0 and set(rows) >= {"a", "b"} and rows["b"].get("loaded"), label + ": both Mods load " + run.stderr[-300:])
    sections = rows.get("b", {}).get("sections", [])
    free = [s for s in sections if s.get("component") in (1, 2)]
    remapped = [s for s in sections if s.get("component") == 0]
    # Component 1 draws; component 2 is the fixture's hidden one (planned, skipped, no draw).
    checks.check(free and all(s.get("planned") and s.get("skip") for s in free) and any(s.get("draws") for s in free),
                 label + ": its components without LOD remaps plan whatever LOD the Core holds " + str([s.get("reason", "") for s in free]))
    checks.check(remapped and all(not s.get("planned") and "LOD contract" in s.get("reason", "") for s in remapped),
                 label + ": its component with a LOD1 remap must agree with the draw's LOD")


def directory_generation(checks, validation):
    """D-078: a Mods folder instead of packages. The fixture Mod and another character's Mod (whose
    sections match no draw of the scene) sit in it as an EFMI Mods folder holds them; the layer reads
    both INIs, makes their packages from what the original engine declares, and must then behave as
    with the fixture's package: the scene's keys, reloads, exchanges and pixels."""
    root = Path(test_dir("runtime-mods-"))
    mods = root / "Mods"
    folder = mods / "fixture"
    (mods / "other").mkdir(parents=True)
    folder.mkdir()
    fixture(mods / "other", salt=0x5A5A5A5A, settings="other", name="other.bin", namespace="runtime_other")
    fixture(folder)
    # LIMIT-041: the fixture's first section chooses its component by `vb1->stride`, the branch the scene's
    # pipeline does not bind (stride 12) first: only the draw's own stride (8) may take its draws.
    stride_branch_ini(folder / "mod.ini", ((12, 1, 1), (8, 0, 0)))
    # D-131: a third character Mod the layer cannot take (an export whose TextureOverride sections are
    # missing, so the Core's contract has no mesh section): the folder generation leaves out that Mod
    # alone, with its reason, and the others behave as before.
    (mods / "broken").mkdir()
    fixture(mods / "broken", salt=0x3C3C3C3C, settings="broken", name="broken.bin", namespace="runtime_broken")
    broken_ini = mods / "broken" / "mod.ini"
    broken_ini.write_text(broken_ini.read_text(encoding="ascii").replace("[TextureOverride", "[CommandListUnused"), encoding="ascii")
    (mods / "DISABLED old").mkdir()
    (mods / "DISABLED old" / "mod.ini").write_text((folder / "mod.ini").read_text(encoding="ascii"), encoding="ascii")
    settings = root / "settings"
    extra = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1", EFMI_VK_M2_TEXTURE_PRELOAD="1", EFMI_VK_M2_IDENTITY_EXPIRY_MS="500",
                 EFMI_VK_TEST_KEYS=str(folder / "keys.txt"),
                 VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                 XDG_CACHE_HOME=str(root), TEMP=str(root), TMP=str(root), TMPDIR=str(root))
    env = layer_env(str(root / "layer.log"), extra=extra)
    env["VK_LAYER_PATH"] = extra["VK_LAYER_PATH"]
    args = [str(Path(DIST) / "efmi_vk_variant_gpu.exe"), "--character", "--material", "--validation", "--lazy-packages",
            "--module", str(Path(DIST) / "efmi_vk_backend.dll"), "--bad-module", str(Path(DIST) / "efmi_vk_backend_mismatch.dll")]
    sources = {p: p.read_bytes() for p in mods.rglob("*") if p.is_file()}
    result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors="replace", timeout=120)
    output = result.stdout + result.stderr
    (root / "stdout.txt").write_text(output.encode("ascii", "backslashreplace").decode(), encoding="ascii")
    label = "Mods folder (directory generation)"
    checks.equal(result.returncode, 0, label + ": the scene's keys, reloads, exchanges and pixels as with the fixture's package")
    checks.check("validation: errors=0 warnings=0" in output, label + ": clean core/sync validation")
    if result.returncode:
        print(output[-6000:]); print("artifacts:", root)
    report = folder / "runtime-final.json"
    status = json.loads(report.read_text())["result"]["replacement"] if report.is_file() else {}
    packages = status.get("packages", [])
    names = [Path(p.get("path", "")).parent.name for p in packages]
    checks.equal(names, ["fixture", "other"], label + ": both character INIs found, the DISABLED folder skipped")
    held = [(p.get("package_bytes", 0) > 0, p.get("payload_held_bytes", -1)) for p in packages]
    checks.equal(held, [(True, 0)] * len(packages), label + ": no package holds its mesh bytes in host memory (ISSUE-011)")
    left_out = status.get("folder_generation", {}).get("left_out", [])
    checks.equal([Path(m.get("ini", "")).parent.name for m in left_out], ["broken"],
                 label + ": the Mod the layer cannot take is left out alone, with its reason " + str([m.get("reason", "") for m in left_out]))
    if len(packages) == 2:
        own, other = packages
        checks.equal((other["lazy"], other["activated"], other.get("images_ready", 0), other["counters"].get("replaced", 0)),
                     (True, False, 0, 0), label + ": the other character's Mod never matched and never went to the GPU")
        checks.check(own["activated"] and own["counters"].get("replaced", 0) > 0 and own["images_ready"] == 2,
                     label + ": the fixture's Mod went to the GPU on its first match and replaced draws")
        checks.equal((own["ini_generation"], own["ini_reloads"]), (2, 2), label + ": its reloads as with a package")
        variables = own.get("ini", {}).get("variables", {})
        checks.equal(variables.get("$\\runtime_fixture\\tint"), 1, label + ": the persistent variable survives F10")
    # One generation for the folder (D-131) keeps one settings file for every Mod, as the original's
    # d3dx_user.ini; a generation per Mod kept the namespace and the Mod folder (BUG-039).
    saved = settings / "mods-folder.ini"
    checks.check(saved.is_file() and "$\\runtime_fixture\\tint = 1" in saved.read_text(), label + ": settings saved in the configured folder")
    # The scene itself edits its mod.ini, drives its keys through keys.txt and writes its reports where
    # it runs (the fixture's folder).
    after = {p: p.read_bytes() for p in mods.rglob("*") if p.is_file()}
    changed = [str(p.relative_to(mods)) for p in after if p.name not in ("mod.ini", "keys.txt") and p in sources and sources[p] != after[p]]
    added = [str(p.relative_to(mods)) for p in after if p not in sources and
             not (p.parent == folder and re.fullmatch(r"(character|runtime)-[\w-]+\.json|pixels\.rgba|stdout\.txt", p.name))]
    checks.equal((changed, added), ([], []), label + ": the layer writes nothing into the Mods folder")
    log = (root / "layer.log").read_text(encoding="utf-8", errors="replace")
    checks.check(log.count("character Mod of the folder generation") >= 2, label + ": every package was made from its INI in the folder generation")
    checks.check("differs from the resolved native identity" not in log,
                 label + ": a section chosen by vb1->stride takes only the draws whose pipeline binds its stride (LIMIT-041)")


if __name__ == "__main__":
    raise SystemExit(main())
