"""Source-defined INI syntax regression through the original compiled parser.

Inventories pinned parser dispatch, operators, operands, target members, resource
and pipeline declarations. These tests check parsing, not GPU execution.
"""
from __future__ import annotations
import json
import hashlib
from pathlib import Path
import subprocess
import sys

from ini_language import inventory

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/ini-language"
HOST = ROOT / "build/xxmi-host/host_probe.exe"

# Arguments exercise source parser alternatives, independent of installed Mods.
COMMANDS = {
    "checktextureoverride": ["checktextureoverride = ps-t0"],
    "run": ["run = CommandListWork", "run = CustomShaderWork"],
    "preset": ["preset = PresetWork"], "exclude_preset": ["exclude_preset = PresetWork"],
    "handling": ["handling = skip", "handling = abort"],
    "reset_per_frame_limits": ["reset_per_frame_limits = ResourceData"],
    "clear": ["clear = ResourceData 0 0 0 0"],
    "analyse_options": ["analyse_options = dump_rt"],
    "dump": ["dump = buf ResourceData"],
    "special": ["special = upscaling_switch_bb", "special = draw_3dmigoto_overlay"],
    "store": ["store = $value, ResourceData, 0"],
    "commandlist": ["CommandListAlias = ref CommandListWork", "CommandListAlias = null"],
    "draw": ["draw = 3,0", "draw = auto", "draw = from_caller"], "drawauto": ["drawauto = 0"],
    "drawindexed": ["drawindexed = 3,0,0", "drawindexed = auto"],
    "drawinstanced": ["drawinstanced = 3,2,0,0"],
    "drawindexedinstanced": ["drawindexedinstanced = 3,2,0,0,0", "drawindexedinstanced = auto"],
    "dispatch": ["dispatch = 1,1,1"],
    "drawindexedinstancedindirect": ["drawindexedinstancedindirect = ResourceData,0"],
    "drawinstancedindirect": ["drawinstancedindirect = ResourceData,0"],
    "dispatchindirect": ["dispatchindirect = ResourceData,0"],
}
PREAMBLE = """namespace = language
[Constants]
global $value = 0
[ResourceData]
type = Buffer
format = R32_UINT
data = 0 1 2 3 4
[PoolData]
pool_size = 4
[PoolOther]
pool_size = 4
[CommandListWork]
$value = $value + 1
[CommandListAlias]
[PresetWork]
$value = 2
[CustomShaderWork]
cs = fixture.hlsl
flags = optimization_level3
[CommandListProbe]
"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    host_report = json.loads((HOST.parent / "report.json").read_text())
    fingerprint = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if (not host_report.get("passed") or fingerprint(HOST) != host_report.get("executable_sha256") or
            any(fingerprint(p) != sha for p, sha in host_report["source_sha256"].items())):
        raise RuntimeError("Original parser host is stale; run scripts/probe_xxmi_host.py --reuse first")
    schema = inventory()
    (OUT / "source-inventory.json").write_text(json.dumps(schema, indent=2) + "\n", encoding="ascii")
    missing = set(schema["dispatch_keywords"]) - COMMANDS.keys()
    stale = COMMANDS.keys() - set(schema["dispatch_keywords"])
    if missing or stale:
        raise ValueError(f"Source command coverage changed: missing={sorted(missing)}, stale={sorted(stale)}")
    fixture = OUT / "fixture"
    (fixture / "Core").mkdir(parents=True, exist_ok=True)
    (fixture / "Mods").mkdir(exist_ok=True)
    (fixture / "Core/main.ini").write_text("; Source grammar fixture\n", encoding="ascii")
    results = []

    def parse(name, text, valid=True):
        (fixture / "Mods/syntax.ini").write_text(text, encoding="ascii")
        run = subprocess.run([str(HOST), "--parse-folder", str(fixture), str(fixture / "Core"), str(fixture / "Mods")],
                             cwd=OUT, capture_output=True, text=True, timeout=20)
        try:
            details = json.loads(run.stdout)
        except ValueError:
            details = {}
        # A crash or failed host setup is not evidence of a rejected syntax.
        passed = (run.returncode == (0 if valid else 1) and bool(details) and
                  (details.get("diagnostics", 0) == 0) == valid)
        row = dict(case=name, passed=passed, expected_valid=valid)
        if details.get("notices"):
            row["upstream_notices"] = details["notices"]
        if not row["passed"]:
            row["diagnostics"] = run.stderr
        results.append(row)

    for keyword, variants in COMMANDS.items():
        for index, command in enumerate(variants):
            parse(f"command:{keyword}:{index}", PREAMBLE + command + "\n")
    for operator in schema["operators"]:
        name, token = operator["name"], operator["token"]
        unary = name.startswith(("unary_", "bitwise_not_")) or token[0].isalpha()
        expression = f"{token}(1)" if unary else f"(4) {token} (2)"
        parse("operator:" + name, PREAMBLE + "$value = " + expression + "\n")
    for operand in schema["enum_tables"]["ParamOverrideTypeNames"]:
        parse("operand:" + operand["token"], PREAMBLE + "$value = " + operand["token"] + "\n")
    for kind, members in schema["target_members"].items():
        for member in members:
            args = ["POSITION" if t == "String" else "0.5" if t == "Float" else "0" for t in member["arguments"]]
            suffix = member["token"] + ("(" + ",".join(args) + ")" if args else "")
            targets = ("vs", "hs", "ds", "gs", "ps", "cs") if kind == "ShaderTarget" else ("vb0",)
            for target in targets:
                line = "$value = " + target + suffix
                if member["token"] == "->region":
                    line = "ResourceData = copy " + target + suffix
                parse(f"member:{kind}:{target}:{member['token']}", PREAMBLE + line + "\n")
                if args:
                    parse(f"bad-member-arity:{kind}:{target}:{member['token']}",
                          PREAMBLE + "$value = " + target + member["token"] + "()\n", False)
    for pattern in schema["target_patterns"]["fixed"]:
        for stage in ("vhdgpc" if "%lc" in pattern else "_"):
            if "s-u" in pattern and stage not in "pc":
                continue
            target = pattern.replace("%lc", stage).replace("%u", "0").replace("%n", "")
            parse("target:" + target, PREAMBLE + "ResourceData = ref " + target + "\n")
    for pattern in schema["target_patterns"]["dynamic"]:
        for stage in ("vhdgpc" if pattern.startswith("s-") else "_"):
            if pattern == "s-u" and stage not in "pc":
                continue
            target = (stage if stage != "_" else "") + pattern
            parse("dynamic-target:" + target, PREAMBLE + f"ResourceData = ref {target}[$value + 1]\n")
            if pattern in ("s-t", "s-u", "s-cb"):
                parse("range-target:" + target, PREAMBLE + f"{target}[$value:2] = ref PoolData[0:2]\n")
    variants = {
        "resource-null": "ResourceData = null", "resource-id": "$value = @ResourceData",
        "pool-variable": "$PoolData[1] = $value + 2", "pool-all-variables": "$PoolData[*] = 1",
        "pool-all-variable-null": "$PoolData[*] = null", "pool-element": "PoolData[$value] = ref ResourceData",
        "pool-all-null": "PoolData[*] = null", "pool-null": "PoolData = null",
        "pool-ref": "PoolOther = ref PoolData", "pool-desc": "PoolOther = copy_desc PoolData",
        "pool-index": "$value = #PoolData[0]", "pool-size": "$value = PoolData->size",
        "pool-identity": "$value = @PoolData", "pool-lastframe": "$value = PoolData[0]->lastframe",
        "pool-value-read": "$value = $PoolData[0]", "layout-format": "vb0->elementformat(POSITION,0) = R32G32B32_FLOAT",
        "layout-offset": "vb0->elementoffset(POSITION,0) = 16", "local": "local $other = 2\n$value = $other",
        "local-uninitialized": "local $other\n$other = 2", "post": "post $value = 2", "pre": "pre $value = 2",
        "flow": "if $value == 0\n$value = 1\nelif $value == 2\n$value = 3\nelse if $value == 4\n$value = 5\nelse\n$value = 6\nendif",
        "format-keyword": "$value = DXGI_FORMAT_R32_FLOAT", "bind-keyword": "$value = D3D11_BIND_VERTEX_BUFFER",
        "hex": "$value = 0x3f800000", "negative-zero": "$value = -0.0", "scientific": "$value = 1.5e-2",
    }
    for c in "xyzw":
        variants["parameter-" + c] = f"{c} = 1\n{c}17 = {c} + 2\n$value = {c}17"
    for c in ("left", "top", "right", "bottom"):
        variants["scissor-" + c] = "$value = scissor3_" + c
    for name, commands in variants.items():
        parse(name, PREAMBLE + commands + "\n")
    for entry in schema["enum_tables"]["ResourceCopyOptionNames"]:
        parse("copy-option:" + entry["token"], PREAMBLE + "ResourceData = " + entry["token"] + " vb0\n")

    resource = dict(array="2", bind_flags="shader_resource", byte_width="64", color_space="sRGB", depth="2",
                    filename="fixture.buf", format="R32_FLOAT", height="2", height_multiply="0.5",
                    max_copies_per_frame="2", mips="1", misc_flags="buffer_structured", msaa="1", msaa_quality="0",
                    stride="4", type="Buffer", width="4", width_multiply="2", data="R32_UINT 0 1 2 3")
    pool = dict(pool_allocate_slot_on_missing="1", pool_element_type_switch_reset="1", pool_expiration_refresh_on_read="1",
                pool_expiration_reset_elements="1", pool_expiration_timeout_frames="30", pool_index_type="ring",
                pool_lazy_initialization="0", pool_persist_variables="1", pool_size="4", pool_spatial_radius="2",
                pool_variable_default_value="1.5")
    def keys(category, examples, section):
        if set(examples) != set(schema["metadata_keys"][category]):
            raise ValueError(f"Missing/stale {category} key examples: {set(examples) ^ set(schema['metadata_keys'][category])}")
        for key, value in examples.items():
            prerequisite = "type = Buffer\n" if category == "Resource" and key == "data" else ""
            parse(f"metadata:{category}:{key}", PREAMBLE + f"\n[{section}]\n{prerequisite}{key} = {value}\n")
    keys("Resource", resource, "ResourceMetadata")
    keys("Pool", pool, "PoolMetadata")
    for table, section, key in (("CustomResourceTypeNames", "ResourceMetadata", "type"),
                                ("CustomColorSpaceNames", "ResourceMetadata", "color_space"),
                                ("CustomResourceBindFlagNames", "ResourceMetadata", "bind_flags"),
                                ("ResourceMiscFlagNames", "ResourceMetadata", "misc_flags"),
                                ("PoolIndexTypeNames", "PoolMetadata", "pool_index_type"),
                                ("D3DCompileFlagNames", "CustomShaderMetadata", "flags")):
        for entry in schema["enum_tables"][table]:
            parse(f"enum:{table}:{entry['token']}", PREAMBLE + f"\n[{section}]\n{key} = {entry['token']}\n")
    for fmt in schema["initial_data_formats"]:
        parse("initial-data:" + fmt, PREAMBLE + f"\n[ResourceInline]\ntype = Buffer\ndata = {fmt} 0 1 0 1\n")
    parse("initial-data:text", PREAMBLE + '\n[ResourceInline]\ntype = Buffer\ndata = "Original text"\n')

    shader = {key: "1" for key in schema["metadata_keys"]["CustomShaderIniKeys"]}
    shader.update({s: "fixture.hlsl" for s in ("vs", "hs", "ds", "gs", "ps", "cs")})
    shader.update(flags="optimization_level3", depth_write_mask="all", depth_func="less_equal",
                  stencil_front="always keep keep keep", stencil_back="always keep keep keep", fill="solid", cull="none",
                  front="CounterClockwise", topology="triangle_list", sampler="linear_filter")
    for key in shader:
        if key == "blend" or key.startswith("blend[") or key == "alpha" or key.startswith("alpha["):
            shader[key] = "add one zero"
    keys("CustomShaderIniKeys", shader, "CustomShaderMetadata")
    shader_enums = dict(BlendOPs="blend = {0} one zero", BlendFactors="blend = add {0} one",
                        DepthWriteMasks="depth_write_mask = {0}", ComparisonFuncs="depth_func = {0}",
                        StencilOps="stencil_front = always {0} keep keep", FillModes="fill = {0}", CullModes="cull = {0}",
                        FrontDirection="front = {0}", PrimitiveTopologies="topology = {0}")
    for table, line in shader_enums.items():
        for value in schema["string_tables"][table]:
            # UNDEFINED is an enum sentinel; the original parser starts at 1.
            parse(f"shader-enum:{table}:{value}", PREAMBLE + "\n[CustomShaderMetadata]\n" + line.format(value) + "\n",
                  not (table == "PrimitiveTopologies" and value == "UNDEFINED"))
    for token in ("activate", "hold", "toggle", "cycle"):
        parse("key-type:" + token, PREAMBLE + f"\n[KeyMetadata]\nkey = no_modifiers VK_F1\ntype = {token}\n$value = " + ("1,2" if token == "cycle" else "1") + "\n")
    for transition in schema["enum_tables"]["TransitionTypeNames"]:
        parse("transition:" + transition["token"], PREAMBLE + f"\n[KeyMetadata]\nkey = CTRL ALT K\ntype = hold\n$value = 1\ntransition = 20\nrelease_transition = 30\ntransition_type = {transition['token']}\nrelease_transition_type = {transition['token']}\n")
    for flag in ("global", "global persist", "global locked", "global persist locked"):
        declaration = PREAMBLE.replace("global $value = 0", "global $value = 0\n" + flag + " $declared = 1")
        parse("variable-flags:" + flag, declaration + "$value = $declared\n")
        if "locked" in flag:
            parse("locked-write:" + flag, declaration + "$declared = 2\n", False)
    key_values = dict(Key="CTRL ALT K", Back="SHIFT K", type="cycle", delay="20", release_delay="30",
                      transition="10", release_transition="20", transition_type="cosine", release_transition_type="linear",
                      condition="$value == 0", run="CommandListWork", wrap="false", smart="true")
    if set(key_values) != set(schema["metadata_keys"]["Key"]):
        raise ValueError("Key option source coverage changed")
    for key, value in key_values.items():
        base = "" if key == "type" else "type = cycle\n"
        base += "" if key == "Key" else "key = CTRL K\n"
        parse("key-option:" + key, PREAMBLE + f"\n[KeyMetadata]\n{base}{key} = {value}\n$value = 1,2\n")
    preset_values = {key: key_values[key] for key in schema["metadata_keys"]["Preset"] if key in key_values}
    preset_values["unique_triggers_required"] = "2"
    keys("Preset", preset_values, "PresetMetadata")
    shader_override = dict(hash="1234567812345678", allow_duplicate_hash="overrule", depth_filter="depth_active",
                           model="ps_5_0", disable_scissor="1", filter_index="10")
    if set(shader_override) != set(schema["metadata_keys"]["ShaderOverrideIniKeys"]):
        raise ValueError("ShaderOverride source coverage changed")
    for key, value in shader_override.items():
        base = "" if key == "hash" else "hash = 1234567812345678\n"
        parse("shader-override:" + key, PREAMBLE + f"\n[ShaderOverrideMetadata]\n{base}{key} = {value}\n")
    for table, key in (("DepthBufferFilterNames", "depth_filter"), ("true_false_overrule", "allow_duplicate_hash")):
        values = [x["token"] for x in schema["enum_tables"][table]] if table in schema["enum_tables"] else schema["string_tables"][table]
        for value in values:
            parse(f"shader-override-enum:{key}:{value}", PREAMBLE + f"\n[ShaderOverrideMetadata]\nhash = 1234\n{key} = {value}\n")
    texture = dict(hash="12345678", format="41", width="8", height="8", width_multiply="2", height_multiply="0.5",
                   override_byte_stride="4", override_vertex_count="8", uav_byte_stride="4", iteration="1,2,3", filter_index="10",
                   expand_region_copy="true", deny_cpu_read="false", match_priority="5", match_type="buffer", match_usage="default",
                   match_bind_flags="+shader_resource -vertex_buffer", match_cpu_access_flags="+read -write", match_misc_flags="+buffer_structured",
                   match_byte_width="128", match_stride="4", match_mips="1", match_format="R32_FLOAT", match_width="8", match_height="8",
                   match_depth="1", match_array="1", match_msaa="1", match_msaa_quality="0",
                   match_first_vertex="0", match_first_index="0", match_first_instance="0", match_vertex_count="8", match_index_count="8", match_instance_count="1")
    if set(texture) != set(schema["metadata_keys"]["TextureOverrideIniKeys"]):
        raise ValueError("TextureOverride source coverage changed")
    fuzzy = set(schema["metadata_keys"]["TextureOverrideFuzzyMatchesIniKeys"])
    for key, value in texture.items():
        base = "" if key == "hash" or key in fuzzy else "hash = 12345678\n"
        if key == "override_vertex_count":
            base += "override_byte_stride = 4\n"
        parse("texture-override:" + key, PREAMBLE + f"\n[TextureOverrideMetadata]\n{base}{key} = {value}\n")
    for value in ("= 8", "! 8", "< 8", "> 8", "<= 8", ">= 8", "res_width / 2", "width * height * 4 / 2"):
        parse("fuzzy-expression:" + value, PREAMBLE + f"\n[TextureOverrideMetadata]\nmatch_byte_width = {value}\n")
    for value in ("res_width / 0", "width + 2", "((8))"):
        parse("invalid-fuzzy:" + value, PREAMBLE + f"\n[TextureOverrideMetadata]\nmatch_byte_width = {value}\n", False)
    for section in schema["sections"]["CommandListSections"]:
        name = section["name"]
        if name in ("Constants", "CommandList", "CustomShader"):
            continue  # Their declarations and commands are already in every fixture.
        extra = "hash = 12345678\n" if name in ("TextureOverride", "ShaderOverride") else ""
        if name == "ShaderRegex":
            extra = "shader_model = ps_5_0\n"
        parse("section:" + name, PREAMBLE + f"\n[{name + ('Metadata' if section['prefix'] else '')}]\n{extra}$value = 1\n")
    regex = "\n[ShaderRegexMetadata]\nshader_model = ps_5_0\ntemps = work\nfilter_index = 42\n[ShaderRegexMetadata.Pattern]\n(?<source>r0)\n[ShaderRegexMetadata.Pattern.Replace]\n${source}\n[ShaderRegexMetadata.InsertDeclarations]\ndcl_temps 1\n"
    parse("shader-regex:parts", PREAMBLE + regex)
    parse("shader-regex:invalid-pattern", PREAMBLE + regex.replace("(?<source>r0)", "("), False)
    for name, text in {"shader-flags": "[CustomShaderBad]\nflags = invalid_flag",
                       "resource-enum": "[ResourceBad]\ntype = invalid_type",
                       "pool-enum": "[PoolBad]\npool_index_type = invalid_type",
                       "initial-data-format": "[ResourceBad]\ntype = Buffer\ndata = R16_FLOAT 1",
                       "shader-depth": "[CustomShaderBad]\ndepth_func = invalid_func"}.items():
        parse("invalid:" + name, PREAMBLE + "\n" + text + "\n", False)
    parse("unknown-command", PREAMBLE + "not_a_command = 1\n", False)
    parse("unknown-operand", PREAMBLE + "$value = not_an_operand\n", False)
    parse("invalid-flow", PREAMBLE + "if $value\n$value = 1\n", False)
    report = dict(scope=__doc__.strip(), passed=all(row["passed"] for row in results), cases=len(results), results=results,
                  inventory=str(OUT / "source-inventory.json"), all_syntax_covered=False, vulkan_execution_complete=False,
                  remaining=["System/driver settings execution", "Texture matcher metadata variants",
                             "Vulkan execution of graphics commands and source shader translation"])
    (OUT / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    for row in results:
        if not row["passed"]:
            print(json.dumps(row, ensure_ascii=True))
    print(f"Source INI language: {sum(row['passed'] for row in results)}/{len(results)} checks passed")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
