"""Validate private fragment-image moves on fingerprinted historical game shaders.

Uses the recorded slot table, not guessed bindings. This is offline bytecode
validation; it neither creates game pipelines nor accepts rendered materials.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"server"))
from efmi_vk.spirv import repoint
from material_profiles import profile_shaders
sys.dont_write_bytecode=True
VAL=Path("F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/install/bin/spirv-val.exe")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_dimension(code: bytes,source: tuple[int,int]) -> int:
    words=struct.unpack(f"<{len(code)//4}I",code);at=5;types={};variables={};decorations={}
    while at<len(words):
        count,op=words[at]>>16,words[at]&65535;args=words[at+1:at+count]
        if not count or at+count>len(words):raise ValueError("Invalid shader instruction bounds")
        if op in (25,32):types[args[0]]=(op,args)
        if op==59:variables[args[1]]=args[0]
        if op==71 and args[1] in (33,34):decorations.setdefault(args[0],{})[args[1]]=args[2]
        at+=count
    ident,=[i for i,d in decorations.items() if (d.get(34),d.get(33))==source]
    pointer=types[variables[ident]];image=types[pointer[1][2]]
    if pointer[0]!=32 or image[0]!=25:raise ValueError("Mapped binding is not a direct image")
    return image[1][2]


def audit_dx_dimensions(refused: list,slots: dict) -> dict:
    # Reuse the read-only sibling capture decoder. No client connection or game IO.
    sys.path.insert(0,"F:/EFMI MCP/server")
    from efmi_mcp import capture_model,hashes,shader as dx_shader
    pairing=json.loads((ROOT/slots["sources"]["pairing"]["path"]).read_text())
    inputs={};captures=[]
    for label in ("frame-a","frame-on"):
        path=ROOT/"captures/dx11-baseline"/label
        header=path.with_suffix(".json");payload=path.with_suffix(".bin")
        cap=capture_model.Capture(json.loads(header.read_text()),payload.read_bytes())
        captures.append((label,cap,list(cap.draws())))
        for p in (header,payload):inputs[str(p)]=digest(p)
    for row in refused:
        pair,=[p for obj in pairing["objects"] for p in obj.get("pairs",[])
               if p.get("vk_fs")==row["hash"]]
        original,=[t for t in pair["textures"]["pairs"] if t["dx"]==row["slot"] and t["vk"]==[row["set"],row["binding"]]]
        if original["what"][0]!="3d":raise ValueError("Original DX11/Vulkan correspondence is not 3d")
        row.update(original_shape=original["what"],dx_shader=pair["dx_ps"],dx_original_draw=pair["dx_draw"],dx_mod_draws=[])
        for label,cap,draws in captures:
            selected=[d for d in draws if d["type"]<8 and d["origin"]=="xxmi" and d["shaders"].get("ps")==pair["dx_ps"]]
            if not selected:raise ValueError("No historical mod-on DX11 draw for dimension audit")
            for draw in selected:
                bound=(draw.get("tex") or {}).get(row["slot"]);resource=cap.resource(bound["res"]) if bound else None
                if not resource or resource["kind"]!="tex2d" or resource["origin"]!="xxmi":
                    raise ValueError("Historical DX11 override is not the expected mod-owned 2d resource")
                row["dx_mod_draws"].append(dict(capture=label,draw=draw["index"],width=resource["w"],height=resource["h"],format=resource["fmt_name"]))
        bytecodes=list(Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/output").rglob(row["dx_shader"]+".bin"))
        if not bytecodes:raise ValueError("Saved DX11 shader bytecode is unavailable")
        bytecode=bytecodes[0];code=bytecode.read_bytes()
        if format(hashes.fnv64(code),"016x")!=row["dx_shader"]:raise ValueError("DX11 bytecode does not match captured shader hash")
        listing=dx_shader.disassemble(code);register=row["slot"].split("-")[1]
        declaration=re.search(r"(?m)^dcl_resource_texture3d\s+.*\b"+re.escape(register)+r"\b",listing)
        samples=[line.strip() for line in listing.splitlines() if re.search(r"\bsample_l_indexable\(texture3d\).*\b"+re.escape(register)+r"\.",line)]
        if not declaration or not samples:raise ValueError("Expected historical DX11 3D declaration/sample is absent")
        row.update(dx_bytecode=str(bytecode),dx_resource_declaration=declaration[0],dx_resource_samples=samples)
        inputs[str(bytecode)]=digest(bytecode)
    inputs[str(Path(capture_model.__file__))]=digest(Path(capture_model.__file__))
    for module in (hashes,dx_shader):inputs[str(Path(module.__file__))]=digest(Path(module.__file__))
    return inputs


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slots",type=Path,default=ROOT/"docs/arcane-texture-slots.json")
    parser.add_argument("--spirv-val",type=Path,default=VAL)
    args=parser.parse_args()
    folder=ROOT/"build/fragment-images";folder.mkdir(parents=True,exist_ok=True)
    report={"scope":"historical game fragment bytecode, no live pipeline/material execution","passed":False}
    try:
        build=json.loads((ROOT/"build/assets-gpu/report.json").read_text())
        exe=ROOT/"build/assets-gpu/asset_gpu.exe"
        if not build.get("passed") or digest(exe)!=build["executable_sha256"]:
            raise ValueError("First build/pass scripts/check_image_upload.py")
        for path,sha in build["source_sha256"].items():
            if digest(Path(path))!=sha:raise ValueError("Rebuild changed asset fixture source: "+path)
        slots=json.loads(args.slots.read_text())
        profiles=profile_shaders(slots)
        rows=[];refused=[];null_rows=[];source_validated=0
        for shader_hash,bindings in sorted(slots["slots"].items()):
            shader=profiles[shader_hash];path=shader["path"];original=path.read_bytes()
            valid=subprocess.run([str(args.spirv_val),"--target-env","vulkan1.1",str(path)],cwd=folder,
                                 capture_output=True,text=True,timeout=15)
            if valid.returncode:raise ValueError("Invalid original fragment SPIR-V: "+valid.stderr)
            source_validated+=1
            # Exercise the generic null operation on all corresponding images,
            # including 3D. This is adapter coverage, not a policy that converts
            # a mismatched 2D replacement into null. Only explicit INI nulls
            # may request this operation in the frontend.
            null_bindings=sorted({(item["set"],item["binding"]) for item in bindings.values()})
            null_output=folder/(shader_hash+"-null.spv")
            argv=[str(exe),"--null-fragment",str(path),str(null_output)]
            for binding in null_bindings:argv += [str(n) for n in binding]
            converted=subprocess.run(argv,cwd=folder,capture_output=True,text=True,timeout=15)
            if converted.returncode:raise ValueError(f"{shader_hash} null: {converted.stderr.strip()}")
            valid=subprocess.run([str(args.spirv_val),"--target-env","vulkan1.1",str(null_output)],cwd=folder,
                                 capture_output=True,text=True,timeout=15)
            if valid.returncode:raise ValueError(f"{shader_hash} null validation: {valid.stderr.strip()}")
            null_rows.append(dict(hash=shader_hash,bindings=null_bindings,output_sha256=digest(null_output),
                                  valid=True,scope="explicit-null adapter coverage only"))
            # A correspondence between original game images is not proof that
            # a mod's replacement view is legal for that shader dimension.
            for slot,item in bindings.items():
                if item.get("sample_2d_replacement")!="refused_dimension_mismatch":continue
                if image_dimension(original,(item["set"],item["binding"]))!=2:
                    raise ValueError("Compatibility annotation does not match actual 3d shader type")
                output=folder/(shader_hash+"-refused.spv")
                converted=subprocess.run([str(exe),"--repoint-fragment",str(path),str(output),
                                          str(item["set"]),str(item["binding"]),"3","2"],
                                         cwd=folder,capture_output=True,text=True,timeout=15)
                if not converted.returncode or "fragment_source_not_float_2d_color_image" not in converted.stderr:
                    raise ValueError("Known incompatible shader dimension did not refuse")
                refused.append(dict(hash=shader_hash,slot=slot,set=item["set"],binding=item["binding"],
                                    reason="2d_asset_for_3d_shader_image"))
            source_bindings=sorted({(item["set"],item["binding"]) for item in bindings.values()
                                    if not item.get("sample_2d_replacement")})
            if not source_bindings:continue
            moves={source:(3,n+2) for n,source in enumerate(source_bindings)}
            expected=repoint(original,moves);output=folder/(shader_hash+".spv")
            argv=[str(exe),"--repoint-fragment",str(path),str(output)]
            for source,target in moves.items():argv += [str(n) for n in (*source,*target)]
            converted=subprocess.run(argv,cwd=folder,capture_output=True,text=True,timeout=15)
            if converted.returncode:raise ValueError(f"{shader_hash}: {converted.stderr.strip()}")
            if output.read_bytes()!=expected:raise ValueError("Native fragment rewrite differs from literal-only reference")
            valid=subprocess.run([str(args.spirv_val),"--target-env","vulkan1.1",str(output)],cwd=folder,
                                 capture_output=True,text=True,timeout=15)
            if valid.returncode:raise ValueError(f"{shader_hash}: SPIR-V validation failed: {valid.stderr.strip()}")
            rows.append(dict(hash=shader_hash,shader=shader["id"],sha256=digest(path),rewritten_sha256=digest(output),
                             moved_images=len(moves),reference_equal=True,source_and_output_valid=True))
        dx_inputs=audit_dx_dimensions(refused,slots)
        report.update(passed=True,shaders=rows,moved_images=sum(r["moved_images"] for r in rows),expected_refusals=refused,
                      dx_audit_inputs=dx_inputs,source_shaders_validated=source_validated,
                      null_shaders=null_rows,null_bindings=sum(len(row["bindings"]) for row in null_rows),
                      source_table_sha256=digest(args.slots),executable_sha256=digest(exe),validator_sha256=digest(args.spirv_val))
        print(f"fragment images: {len(rows)} shaders, {report['moved_images']} image moves; reference/spirv-val passed; {len(refused)} expected dimension refusals")
        print(f"fragment nulls: {len(null_rows)} shaders, {report['null_bindings']} bindings; spirv-val passed (adapter coverage)")
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        report["error"]=str(error);print(str(error).encode("ascii","backslashreplace").decode("ascii"),file=sys.stderr);return 1
    finally:
        (folder/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="ascii")


if __name__=="__main__":raise SystemExit(main())
