"""Retained references to application images require current GPU layout facts."""
from pathlib import Path
import json
import shutil
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader
from native_shader_test import colored_pixels
from runtime_test import sandbox_shader_hashes
from native_compute_test import read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def scene(checks, case):
    root=Path(test_dir("native-application-image-"+case+"-"))
    source=root/"scene.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
        "[[vk::binding(2,0)]] Texture2D<float4> input;\n"
        "[numthreads(1,1,1)] void main() { output[0] += uint(round(input.Load(int3(0,0,0)).r * 255)); }\n",encoding="ascii")
    shader=compile_shader(source,root/"compiled","12345678","cs")
    mods=root/"Mods";mods.mkdir()
    body="ResourceHeld = ref cs-t0\ncs-t0 = ref ResourceHeld\n"
    if case=="retained":body="if $saved == 0\nResourceHeld = ref cs-t0\n$saved = 1\nendif\ncs-t0 = ref ResourceHeld\n"
    (mods/"test.ini").write_text("namespace = application_image\n[Constants]\nglobal $saved = 0\n[ResourceHeld]\n"
        "[ShaderOverrideCompute]\nhash = %08x\nvk_binding = cs-t0, 0, 2\n"%crc32c(shader.read_bytes())+body,encoding="ascii")
    output=root/"result.bin"
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_ENABLED="1",VK_LAYER_VALIDATE_SYNC="1")
    args=["--compute-shader",str(shader),"--compute-output",str(output),"--compute-image","--compute-repeat","2"]
    if case=="push":args += ["--descriptors","push"]
    if case=="secondary":args.append("--compute-secondary")
    if case=="two_queues":args.append("--two-queues")
    if case=="sync2":args.append("--sync2")
    if case=="mutable":args.append("--srgb-texture-view")
    with Sandbox(seconds=3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status().get("replacement",{})
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
    checks.equal(struct.unpack_from("<I",read_compute_result(output))[0],0 if case=="mutable" else 2,case+": application image bytes remain correct")
    calls=status.get("shader_overrides",{})
    if case=="two_queues":checks.check("queue" in calls.get("last_reason",""),case+": unsupported queue graph reported")
    elif case=="sync2":checks.check("synchronization2" in calls.get("last_reason",""),case+": unsupported signal scopes reported")
    elif case=="mutable":checks.check("owned optimal" in calls.get("last_reason",""),case+": mutable format image reported")
    else:checks.check(calls.get("executed",0)>0 and not calls.get("last_reason"),case+": application image reference executes without a layout refusal")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(),case+": core and synchronization validation clean")
    (root/"status.json").write_text(json.dumps(status,indent=2),encoding="ascii")
    print("artifacts:",root)


def layout_scene(checks,case):
    root=Path(test_dir("native-application-image-"+case+"-"))
    first=root/"first.hlsl";followup=root/"followup.hlsl"
    common="[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n[[vk::binding(3,0)]] Texture2D<float4> destination;\n"
    first.write_text(common+"[[vk::binding(2,0)]] Texture2D<float4> source;\n[numthreads(1,1,1)] void main() { output[0] += uint(round(source.Load(int3(0,0,0)).r*255)) + 100*uint(round(destination.Load(int3(0,0,0)).r*255)); }\n",encoding="ascii")
    followup.write_text(common+"[numthreads(1,1,1)] void main() { output[0] += 100*uint(round(destination.Load(int3(0,0,0)).r*255)); }\n",encoding="ascii")
    shader=compile_shader(first,root/"compiled","12345678","cs")
    later=compile_shader(followup,root/"compiled","87654321","cs")
    mods=root/"Mods";mods.mkdir()
    (mods/"test.ini").write_text("namespace = image_layout\n[Constants]\nglobal $loaded = 1\n[ResourceHeld]\n"
        "[ShaderOverrideFirst]\nhash = %08x\nvk_binding = cs-t0, 0, 2\nvk_binding = cs-t1, 0, 3\nResourceHeld = ref cs-t0\ncs-t1 = ref ResourceHeld\n"%crc32c(shader.read_bytes()),encoding="ascii")
    output=root/"result.bin"
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_ENABLED="1",VK_LAYER_VALIDATE_SYNC="1")
    args=["--compute-shader",str(shader),"--compute-output",str(output),"--compute-image-reference",str(later)]
    if case=="layout_push":args += ["--descriptors","push"]
    if case=="layout_secondary":args.append("--compute-secondary")
    with Sandbox(seconds=3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status()
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
    checks.equal(struct.unpack_from("<I",read_compute_result(output))[0],5601,case+": distinct image replacement reads latest GPU writes and application rebind wins")
    images=status.get("device",{}).get("native_images",{})
    checks.check(images.get("transitions",0)>0,case+": temporary layout transition actually used")
    checks.check(images.get("refreshes",0)>0,case+": persistent descriptor refreshed on unmatched pipeline")
    checks.equal(images.get("refused",0),0,case+": all persistent image states supported")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(),case+": layout restoration and synchronization validation clean")
    (root/"status.json").write_text(json.dumps(status,indent=2),encoding="ascii")
    print("artifacts:",root)


def graphics_scene(checks,case):
    root=Path(test_dir("native-application-image-"+case+"-"));_,fragment=sandbox_shader_hashes()
    mods=root/"Mods";mods.mkdir()
    (mods/"test.ini").write_text("namespace = graphics_image\n[Constants]\nglobal $loaded = 1\n[ResourceHeld]\n"
        "[ShaderOverridePixel]\nhash = %08x\nvk_binding = ps-t0, 1, 5\nResourceHeld = ref ps-t0\nps-t0 = ref ResourceHeld\n"%fragment,encoding="ascii")
    settings=root/"settings";fixes=settings/"ShaderFixes";fixes.mkdir(parents=True)
    source=root/"sample.hlsl"
    source.write_text("[[vk::binding(5,1)]] [[vk::combinedImageSampler]] Texture2D<float4> tex;\n"
        "[[vk::binding(5,1)]] [[vk::combinedImageSampler]] SamplerState samp;\n"
        "float4 main() : SV_Target0 { float4 color=tex.SampleLevel(samp,float2(0,0),0); return float4(color.r*255,0,color.g*63.75,1); }\n",encoding="ascii")
    compile_shader(source,fixes,"%08x"%fragment,"ps")
    pixels=root/"pixels.raw"
    keys=root/"keys.txt";keys.write_text("1\n",encoding="ascii");trigger=root/"capture.now"
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
        EFMI_VK_M2_ENABLED="1",EFMI_VK_TEST_NOTICE="> application image",EFMI_VK_TEST_NOTICES_READBACK=str(pixels),
        EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC="1")
    if case in ("graphics_reload","graphics_swap"):env["EFMI_VK_TEST_NOTICES_READBACK_TRIGGER"]=str(trigger)
    args=["--readable-swapchain"]
    if case=="graphics_push":args += ["--descriptors","push"]
    if case=="graphics_secondary":args += ["--threads","2"]
    with Sandbox(seconds=6 if case in ("graphics_reload","graphics_swap") else 3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case=="graphics_swap":
                module=root/"native-backend.dll";shutil.copy2(Path(__file__).resolve().parents[1]/"dist/efmi_vk_backend.dll",module)
                probe.swap_module(str(module))
                deadline=time.monotonic()+1
                while time.monotonic()<deadline:
                    current=probe.status().get("replacement",{})
                    if current.get("module",{}).get("generation",0)>0 and current.get("shader_fixes",{}).get("binds",0)>0:break
                    time.sleep(.02)
                trigger.touch()
            if case=="graphics_reload":
                source.write_text(source.read_text().replace("return float4(color.r*255,0,color.g*63.75,1);", "return float4(0,color.r*255,0,1);"),encoding="ascii")
                compile_shader(source,fixes,"%08x"%fragment,"ps")
                keys.write_text("1 121\n",encoding="ascii");time.sleep(.2);keys.write_text("1\n",encoding="ascii");time.sleep(1);trigger.touch()
            status=probe.status()
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
    calls=status.get("replacement",{}).get("shader_overrides",{})
    if case=="graphics_swap":checks.check(status.get("replacement",{}).get("module",{}).get("generation",0)>0,case+": exchanged backend active")
    if case=="graphics_swap":
        fixes=status.get("replacement",{}).get("shader_fixes",{})
        checks.check(fixes.get("loaded")==1 and fixes.get("binds",0)>0 and not fixes.get("warnings"),case+": exchanged module validates and binds ShaderFixes")
        checks.check(pixels.is_file(),case+": post-exchange capture completed")
    checks.check(calls.get("executed",0)>0 and not calls.get("last_reason"),case+": in-pass retained application image executes")
    checks.check(colored_pixels(pixels,(0,255,0) if case=="graphics_reload" else (255,0,255))>100,case+": shader reads actual application texels")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(),case+": Vulkan and synchronization validation clean")
    (root/"status.json").write_text(json.dumps(status,indent=2),encoding="ascii");print("artifacts:",root)


def main():
    checks=Checks("native application images");selected=set(sys.argv[1:])
    for case in ("direct","retained","push","secondary","two_queues","sync2","mutable"):
        if not selected or case in selected:scene(checks,case)
    for case in ("layout","layout_push","layout_secondary"):
        if not selected or case in selected:layout_scene(checks,case)
    for case in ("graphics","graphics_push","graphics_secondary","graphics_reload","graphics_swap"):
        if not selected or case in selected:graphics_scene(checks,case)
    return checks.finish()


if __name__=="__main__":
    raise SystemExit(main())
