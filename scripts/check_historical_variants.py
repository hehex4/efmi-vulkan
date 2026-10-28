"""Create whole-character LOD0/LOD1 variants from verified saved game recipes.

The child process has no window, game hook or registry installation. This is
driver pipeline creation only: it neither binds the game's resources nor draws
the character. Unknown material correspondences and dimension mismatches remain
explicit omissions, never replaced with guessed contracts.
"""
from __future__ import annotations
from collections import Counter
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from build import COMMON, BuildError, find_vcvars, find_vulkan_include, run_cl
from check_mod_draw_plans import ROOT, SAMPLE, sha
from check_core_components import inputs
from verify_bone_import import sample_layout
from check_material_plans import rules_for, material_requests, expected_contract
sys.path.insert(0, str(ROOT / "sandbox"))
from harness import inject_env


def compact(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True)


def native_draws(data):
    if data[:8] != b"EFMICO02":
        raise ValueError("Wrong original Core replay format")
    at = 12
    for _ in range(struct.unpack_from("<I", data, 8)[0]):
        record, phase, count, _, component, lod = struct.unpack_from("<6I", data, at)
        at += 24
        for draw in range(count):
            at += 20
            size, = struct.unpack_from("<I", data, at+256)
            at += 260+size
            values = []
            for width in (3, 4):
                length, = struct.unpack_from("<I", data, at)
                at += 4
                if length > 256:
                    raise ValueError("Invalid native vertex declaration count")
                values.append([list(struct.unpack_from(f"<{width}I", data, at+i*width*4)) for i in range(length)])
                at += length*width*4
            bindings, attributes = values
            yield record, phase, component, lod, draw, dict(
                bindings=[dict(binding=b, stride=s, per_instance=bool(i)) for b, s, i in bindings],
                attributes=[dict(location=l, binding=b, format=f, offset=o) for l, b, f, o in attributes])
    if at != len(data):
        raise ValueError("Trailing original Core replay data")


def generate_case(case, index):
    p, r = case["pipeline"], case["pipeline"]["recipe"]
    lines = [f"static HistoricalCase case_{index}(){{", "HistoricalCase c;auto& r=c.recipe;r.complete=true;"]
    def emit(text): lines.append(text)
    def fields(prefix, data, mapping):
        if set(data) != set(mapping):
            raise ValueError(f"Unrepresented metadata in {prefix}: {set(data) ^ set(mapping)}")
        for name, member in mapping.items():
            v = data[name]
            if not isinstance(v, (int, bool)) or not -(1 << 31) <= v < (1 << 32):
                raise ValueError("Invalid numeric pipeline field")
            emit(f"{prefix}.{member}=float_bits({v}u);" if name.endswith("_bits") else f"field({prefix}.{member},{int(v)}LL);")
    def vec(prefix, values):
        emit(prefix+"={"+",".join(str(int(v))+"u" for v in values)+"};")
    def vertex(prefix, v):
        for b in v["bindings"]:
            emit("{VkVertexInputBindingDescription b{};")
            fields("b", b, dict(binding="binding", stride="stride", per_instance="inputRate"))
            emit(prefix+".bindings.push_back(b);}")
        for a in v["attributes"]:
            emit("{VkVertexInputAttributeDescription a{};")
            fields("a", a, dict(location="location", binding="binding", format="format", offset="offset"))
            emit(prefix+".attributes.push_back(a);}")
    def refs(prefix, values):
        for value in values:
            emit("{VkAttachmentReference2 a{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};")
            fields("a", value, dict(attachment="attachment", layout="layout", aspect_mask="aspectMask"))
            emit(prefix+".push_back(a);}")
    fields("r", {k:r[k] for k in ("flags", "subpass", "viewport_count", "scissor_count")},
           dict(flags="flags", subpass="subpass", viewport_count="viewport_count", scissor_count="scissor_count"))
    rp = r["render_pass"]
    if not r["complete"] or r["reason"] or not rp["complete"] or rp["reason"] or rp["version"] not in (1, 2):
        raise ValueError("Incomplete original graphics recipe")
    emit("auto rp=std::make_shared<efvk::RenderPassRecipe>();rp->complete=true;")
    emit(f"rp->id={rp['id']}u;rp->flags={rp['flags']}u;rp->version2={int(rp['version']==2)};")
    for a in rp["attachments"]:
        emit("{VkAttachmentDescription2 a{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};")
        fields("a", a, dict(flags="flags", format="format", samples="samples", load="loadOp", store="storeOp",
                           stencil_load="stencilLoadOp", stencil_store="stencilStoreOp", initial_layout="initialLayout", final_layout="finalLayout"))
        emit("rp->attachments.push_back(a);}")
    for s in rp["subpasses"]:
        emit("{efvk::RenderSubpass s;")
        fields("s", {k:s[k] for k in ("flags", "bind_point", "view_mask")}, dict(flags="flags", bind_point="point", view_mask="view_mask"))
        for name in ("inputs", "colors", "resolves"):
            refs("s."+name, s[name])
        if len(s["depth"]) > 1:
            raise ValueError("Multiple depth attachments")
        if s["depth"]:
            emit("s.has_depth=true;")
            fields("s.depth", s["depth"][0], dict(attachment="attachment", layout="layout", aspect_mask="aspectMask"))
        vec("s.preserves", s["preserves"])
        emit("rp->subpasses.push_back(s);}")
    for d in rp["dependencies"]:
        emit("{VkSubpassDependency2 d{VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2};")
        fields("d", d, dict(src_subpass="srcSubpass", dst_subpass="dstSubpass", src_stage="srcStageMask", dst_stage="dstStageMask",
                           src_access="srcAccessMask", dst_access="dstAccessMask", flags="dependencyFlags", view_offset="viewOffset"))
        emit("rp->dependencies.push_back(d);}")
    vec("rp->correlated", rp["correlated_view_masks"])
    emit("r.render_pass=rp;")
    for s, file in zip(r["stages"], case["shader_files"]):
        emit("{efvk::RecipeStage s;")
        emit(f"field(s.stage,{s['stage']});s.entry={compact(s['entry'])};s.shader=shader_file({compact(str(file))},{s['shader']}u);")
        for e in s["specialization_entries"]:
            emit(f"s.entries.push_back({{{e['id']}u,{e['offset']}u,{e['size']}u}});")
        vec("s.data", s["specialization_bytes"])
        emit("r.stages.push_back(s);}")
    vertex("r.vertex", p)
    fields("r.assembly", r["assembly"], dict(topology="topology", primitive_restart="primitiveRestartEnable"))
    fields("r.raster", r["raster"], dict(depth_clamp="depthClampEnable", discard="rasterizerDiscardEnable", polygon="polygonMode", cull="cullMode",
           front_face="frontFace", depth_bias="depthBiasEnable", bias_constant_bits="depthBiasConstantFactor", bias_clamp_bits="depthBiasClamp",
           bias_slope_bits="depthBiasSlopeFactor", line_width_bits="lineWidth"))
    for v in r["viewports"]:
        emit("{VkViewport v{};")
        fields("v", v, dict(x_bits="x", y_bits="y", width_bits="width", height_bits="height", min_depth_bits="minDepth", max_depth_bits="maxDepth"))
        emit("r.viewports.push_back(v);}")
    for s in r["scissors"]:
        emit(f"r.scissors.push_back({{{{{s['x']},{s['y']}}},{{{s['width']}u,{s['height']}u}}}});")
    if r["multisample"] is not None:
        m = r["multisample"]
        fields("r.multisample", {k:v for k,v in m.items() if k != "mask"}, dict(samples="rasterizationSamples", sample_shading="sampleShadingEnable",
               min_sample_shading_bits="minSampleShading", alpha_to_coverage="alphaToCoverageEnable", alpha_to_one="alphaToOneEnable"))
        vec("r.sample_mask", m["mask"])
    if r["depth_stencil"] is not None:
        d = r["depth_stencil"]
        emit("r.has_depth=true;")
        fields("r.depth", {k:v for k,v in d.items() if k not in ("front", "back")}, dict(test="depthTestEnable", write="depthWriteEnable", compare="depthCompareOp",
               bounds_test="depthBoundsTestEnable", stencil_test="stencilTestEnable", min_bounds_bits="minDepthBounds", max_bounds_bits="maxDepthBounds"))
        for side in ("front", "back"):
            fields("r.depth."+side, d[side], {"fail":"failOp", "pass":"passOp", "depth_fail":"depthFailOp", "compare":"compareOp",
                   "compare_mask":"compareMask", "write_mask":"writeMask", "reference":"reference"})
    if r["blend"] is not None:
        b = r["blend"]
        emit("r.has_blend=true;")
        emit(f"r.blend.attachmentCount={len(b['attachments'])}u;")
        fields("r.blend", {k:b[k] for k in ("logic_enable", "logic_op")}, dict(logic_enable="logicOpEnable", logic_op="logicOp"))
        if len(b["constants_bits"]) != 4:
            raise ValueError("Invalid blend constant count")
        for i, value in enumerate(b["constants_bits"]):
            emit(f"r.blend.blendConstants[{i}]=float_bits({value}u);")
        for a in b["attachments"]:
            emit("{VkPipelineColorBlendAttachmentState a{};")
            fields("a", a, dict(enable="blendEnable", src_color="srcColorBlendFactor", dst_color="dstColorBlendFactor", color_op="colorBlendOp",
                               src_alpha="srcAlphaBlendFactor", dst_alpha="dstAlphaBlendFactor", alpha_op="alphaBlendOp", write_mask="colorWriteMask"))
            emit("r.blend_attachments.push_back(a);}")
    for value in p["dynamic_states"]:
        emit(f"r.dynamic.push_back(static_cast<VkDynamicState>({value}));")
    layout = p["layout_recipe"]
    if not layout["complete"] or p["push_constants"]:
        raise ValueError("Incomplete layout or uncaptured push-constant fixture support")
    emit(f"c.layout.flags={layout['flags']}u;")
    for s in layout["sets"]:
        if not s["complete"]:
            raise ValueError("Incomplete original descriptor set")
        emit(f"{{auto s=std::make_shared<efvk::DescriptorLayoutInfo>();s->id={s['id']}u;s->flags={s['flags']}u;")
        for b in s["bindings"]:
            if b["immutable_samplers"]:
                raise ValueError("Immutable samplers lack replayable sampler recipes")
            emit(f"s->bindings[{b['binding']}u]={{{b['type']}u,{b['count']}u,{b['stages']}u,{b['flags']}u,{{}}}};")
        emit("c.layout.sets.push_back(s);}")
    vertex("c.vertex", case["vertex"])
    for name in ("images", "nulls"):
        for s, b in case["patch"][name]:
            emit(f"c.fragment.{name}.push_back({{{s}u,{b}u}});")
    for name, expected in (("recipe", r), ("layout", layout), ("vertex", case["vertex"]), ("patch", case["patch"]),
                           ("source_vertex", {k:p[k] for k in ("bindings", "attributes")}), ("dynamic", p["dynamic_states"])):
        emit(f"c.expected_{name}={compact(compact(expected))};")
    emit("return c;}")
    return "\n".join(lines)


def collect_cases(character_package=None):
    for script in ("check_core_components.py", "check_material_plans.py"):
        subprocess.run([sys.executable, str(ROOT/"scripts"/script)], cwd=ROOT, check=True, capture_output=True, timeout=120)
    replay = json.loads((ROOT/"build/core-components/report.json").read_text())
    path = ROOT/"build/core-components/output.bin"
    if not replay["passed"] or sha(path) != replay["output_sha256"]:
        raise ValueError("Native vertex replay is not current")
    records = inputs(sample_layout(SAMPLE))
    callbacks = json.loads((ROOT/"build/xxmi-host/mod-plans.json").read_text())
    plans = {(int(re.search(r"component(\d+)$", row["section"])[1]), row["lod"]):row for row in callbacks["runs"] if row["scenario"] == 0}
    table = json.loads((ROOT/"docs/arcane-texture-slots.json").read_text())
    cases, folders, counts, omitted = {}, {}, Counter(), []
    evidence = [ROOT/"build/core-components/report.json", path, ROOT/"build/core-components/input.bin",
                ROOT/"build/material-plans/report.json", ROOT/"build/xxmi-host/mod-plans.json", ROOT/"docs/arcane-texture-slots.json"]
    sources = {str(p):sha(p) for p in evidence}
    static = None
    if character_package:
        package = character_package.resolve()
        metadata = package.with_suffix(".json")
        static = json.loads(metadata.read_text())
        if sha(package) != static["package_sha256"] or sha(SAMPLE) != static["ini"]["sha256"]:
            raise ValueError("Static character package fingerprint changed")
        exe = ROOT / "build/next/efmi_vk_character_test.exe"
        decoded = subprocess.run([str(exe), "--materials", str(package)], cwd=ROOT / "build", check=True,
                                 capture_output=True, text=True, timeout=30)
        native = json.loads(decoded.stdout)
        native_profiles = {f"{p['crc']:08x}": p for p in native["profiles"]}
        profile_hashes = {p["fragment"]: p["sha256"] for p in static["materials"]["profiles"]}
        firsts, cursor = {}, 0
        for component in static["components"]:
            for lod, count in enumerate(component["draws"]):
                firsts[component["id"], lod] = cursor
                cursor += count
        if cursor != static["draws"]:
            raise ValueError("Static component draw lists disagree")
        for file in (package, metadata, exe):
            sources[str(file)] = sha(file)
    for filename, fingerprint in replay["source_sha256"].items():
        if sha(Path(filename)) != fingerprint:
            raise ValueError("Original Core evidence changed after replay")
        sources[filename] = fingerprint
    for index, phase, component, lod, draw, vertex in native_draws(path.read_bytes()):
        if phase != 1 or lod not in (0, 1):
            raise ValueError("Unexpected replay phase or out-of-scope LOD")
        r = records[index]
        folder = Path(r["bundle"])
        if folder not in folders:
            manifest_path = folder/"manifest.json"
            manifest = json.loads(manifest_path.read_text())
            sources[str(manifest_path)] = sha(manifest_path)
            def read(name):
                file = folder/name
                if sha(file) != manifest["files"][name]["sha256"]:
                    raise ValueError("Archived pipeline inventory changed")
                return json.loads(file.read_text())
            folders[folder] = (manifest, {p["id"]:p for p in read("pipelines.json")},
                               {s["id"]:s for s in read("shaders.json")}, {d["n"]:d for d in read("frame.json")["draws"]})
        manifest, pipelines, shaders, draws = folders[folder]
        pipeline = pipelines[draws[r["draw"]]["pipeline"]]
        fs, = [s for s in pipeline["shaders"] if s["stage"] == 16]
        request = material_requests(plans[component, lod])[draw]
        contract = expected_contract(request, rules_for(table, fs["hash"]))
        if static:
            item = firsts[component, lod] + draw
            pixels = {int(k): v for k, v in static["materials"]["pixels"][item].items()}
            static_request = bytearray(128)
            for slot, asset in pixels.items():
                static_request[slot] = 1 if asset == 0xFFFFFFFF else 2
            if bytes(static_request) != request:
                raise ValueError("Static pixel bindings differ from the hosted callback")
            key = fs["hash"]
            if pixels:
                if sha(folder / shaders[fs["id"]]["file"]) != profile_hashes.get(key):
                    raise ValueError("Static material profile does not match saved fragment bytes")
                resolved = native_profiles[key]["draws"][item]
                if not resolved["ok"]:
                    raise ValueError("Static production material contract refused: " + resolved["reason"])
                rules = rules_for(table, key)
                adjusted = bytearray(request)
                for slot, rule in rules.items():
                    if rule.get("incompatible") and adjusted[slot] == 2 and static["materials"]["compatibility"]:
                        adjusted[slot] = 1
                expected = expected_contract(adjusted, rules)
                image_assets = [[pixels[slot], s, b] for slot, s, b in expected.get("images", [])]
                if not expected["ok"] or resolved["images"] != image_assets or resolved["nulls"] != expected["nulls"]:
                    raise ValueError("Static native contracts disagree with source correspondence")
                contract = expected
                if resolved["compatibility"]:
                    counts["dimension_zero_draws"] += 1
        if not contract["ok"]:
            counts[contract["refusal"]] += 1
            omitted.append(dict(bundle=str(folder.relative_to(ROOT)), draw=r["draw"], callback_draw=draw,
                                component=component, lod=lod, fragment=fs["hash"], reason=contract["refusal"]))
            continue
        counts["accepted_draws"] += 1
        files = [folder/shaders[s["shader"]]["file"] for s in pipeline["recipe"]["stages"]]
        for file in files:
            if sha(file) != manifest["files"][file.relative_to(folder).as_posix()]["sha256"]:
                raise ValueError("Archived shader code changed")
            sources[str(file)] = sha(file)
        patch = dict(images=[i[1:] for i in contract["images"]], nulls=contract["nulls"])
        key = (str(folder), pipeline["id"], compact(vertex), compact(patch))
        case = cases.setdefault(key, dict(pipeline=pipeline, shader_files=files, vertex=vertex, patch=patch, draws=[]))
        case["draws"].append(dict(bundle=str(folder.relative_to(ROOT)), draw=r["draw"], callback_draw=draw, component=component, lod=lod))
    return list(cases.values()), dict(counts), sources, omitted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--character-package", type=Path, help="verify production static material contracts, including its explicit compatibility policy")
    args = parser.parse_args()
    out = ROOT / ("build/character-variants" if args.character_package else "build/historical-variants")
    out.mkdir(parents=True, exist_ok=True)
    report = dict(scope="historical LOD0/LOD1 driver pipeline creation only; no character drawing or game execution", passed=False)
    try:
        cases, counts, sources, omitted = collect_cases(args.character_package)
        if not cases or len(cases) > 512:
            raise ValueError("Historical case count outside fixture budget")
        generated = "\n".join(generate_case(c, i) for i, c in enumerate(cases))
        generated += f"\nconstexpr unsigned historical_case_count={len(cases)};\nstatic HistoricalCase historical_case(unsigned n){{switch(n){{\n"
        generated += "\n".join(f"case {i}:return case_{i}();" for i in range(len(cases)))
        generated += '\ndefault:throw std::runtime_error("Unknown historical case");}}\n'
        (out/"cases.inc").write_text(generated, encoding="ascii")
        sources_cpp = [ROOT/"sandbox/historical_variant_gpu.cpp"]
        sources_cpp += [ROOT/"frontend/assets"/name for name in ("dds.cpp", "png.cpp", "image_upload.cpp", "resource_cache.cpp", "buffer_upload.cpp", "file_plan.cpp", "file_draws.cpp")]
        sources_cpp += [ROOT/"frontend/gpu"/name for name in ("fragment_images.cpp", "dither_kills.cpp", "material_plan.cpp", "draw_transaction.cpp")]
        sources_cpp += [ROOT/"layer/src"/name for name in ("pipeline_recipe.cpp", "pipeline_variant.cpp", "spirv_repoint.cpp", "variant_cache.cpp", "draw_snapshots.cpp")]
        exe = out/"historical_variants.exe"
        args = COMMON+["/DNOMINMAX", "/I"+find_vulkan_include(), "/I"+str(out), "/I"+str(ROOT/"frontend/assets"),
                       "/Fo"+str(out)+"/", "/Fd"+str(out/"test.pdb")]
        args += [str(p) for p in sources_cpp]+["/Fe:"+str(exe), "/link", "/NOLOGO", "user32.lib"]
        assets = json.loads((ROOT/"build/assets-gpu/report.json").read_text())
        source_files = set(sources_cpp) | {Path(__file__), out/"cases.inc"} | {Path(p) for p in assets["source_sha256"]}
        sources.update({str(p):sha(p) for p in source_files})
        log = io.StringIO()
        try:
            with contextlib.redirect_stdout(log): run_cl(find_vcvars(), str(out), args)
        except BuildError as error:
            log.write(str(error))
            raise
        finally:
            (out/"build.txt").write_text(log.getvalue().encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
        if not (validation/"VkLayer_khronos_validation.json").is_file():
            raise ValueError("Validation layer missing")
        env = inject_env(str(out/"unexpected-layer.log"))
        env.update(VK_LAYER_PATH=str(validation), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                   XDG_CACHE_HOME=str(out), TMPDIR=str(out), TEMP=str(out), TMP=str(out))
        result = subprocess.run([str(exe)], cwd=out, env=env, capture_output=True, encoding="utf-8", errors="replace", timeout=120)
        output = result.stdout+result.stderr
        (out/"runtime.txt").write_text(output.encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        summary = re.search(r"historical_variants: (\d+) created, (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)", output)
        rows = [json.loads(line) for line in output.splitlines() if line.startswith('{"case":')]
        if result.returncode or not summary or any(int(summary[n]) for n in (3, 4, 5)) or int(summary[1]) != len(cases) or len(rows) != len(cases):
            print(output[-5000:])
            raise ValueError("Historical driver creation failed; see runtime.txt")
        if any(row["case"] != i or not row["created"] or row["reason"] or row["result"] for i,row in enumerate(rows)):
            raise ValueError("Historical driver results are incomplete or out of order")
        if any(sha(Path(p)) != fingerprint for p, fingerprint in sources.items()):
            raise ValueError("Historical input changed during verification")
        report.update(passed=True, counts=counts, variants=len(cases), omitted=omitted, executable_sha256=sha(exe), source_sha256=sources,
                      checks_passed=int(summary[2]), validation_errors=0, validation_warnings=0,
                      device=next(json.loads(line) for line in output.splitlines() if line.startswith('{"device":')),
                      cases=[dict(index=i, draws=c["draws"], pipeline=c["pipeline"]["id"], patch=c["patch"]) for i,c in enumerate(cases)])
        print(summary[0]);print(json.dumps(counts, indent=2))
        return 0
    except (OSError, ValueError, KeyError, IndexError, struct.error, BuildError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print(str(error).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (out/"report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True)+"\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
