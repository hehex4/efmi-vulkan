"""Imageless runtime attachments drive output references and native image layout proof."""
from pathlib import Path
import json
import shutil
import sys
import time
from harness import Checks, Sandbox, test_dir, DIST, configuration_root
from native_shader_test import compile_shader, colored_pixels
from runtime_test import sandbox_shader_hashes, band_pixels
from efmi_vk.probe_client import ProbeClient


def scene(checks, case):
    root=Path(test_dir('native-imageless-'+case+'-'));mods=root/'Mods';mods.mkdir()
    _,fragment=sandbox_shader_hashes()
    body='checktextureoverride = o0\nif o0->width > 0 && o0->height > 0 && o0->mips == 1 && o0->array == 1 && o0->format > 0 && o0->bindflags == 32 && o0->size == -3 && $matched == 1\nhandling = skip\nendif\n'
    if case=='absent':body='ResourceSaved = ref o1\nif o1->width == -2 && oD->width == -2 && ResourceSaved->width == -2\nhandling = skip\nendif\n'
    if case=='unowned':body='ResourceSaved = ref o0\nhandling = skip\n'
    (mods/'mod.ini').write_text('namespace = native_target\n[Constants]\nglobal $matched = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nvk_hash = %08x\n'%fragment+body+'[TextureOverrideTarget]\nmatch_type = Texture2D\n$matched = 1\n',encoding='ascii')
    settings=root/'settings';settings.mkdir();pixels=root/'pixels.raw'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output resources',EFMI_VK_TEST_NOTICES_READBACK=str(pixels))
    with Sandbox(seconds=3,threads=2 if case=='secondary' else 0,validation=True,entry='inject' if case=='inject' else 'layer',env_extra=env,extra_args=['--readable-swapchain','--imageless','--api','1.3']) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{})
        (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii')
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    fallback=case in ('secondary','unowned')
    checks.check(pixels.exists() and ((band_pixels(pixels) or 0)>200 if fallback else band_pixels(pixels)<30),case+': attachment queries control actual draws or preserve explicit fallback')
    if fallback:
        note=status.get('shader_overrides',{}).get('last_reason','') if case=='unowned' else (root/'layer.log').read_text(encoding='utf-8')
        checks.check('retained allocation owner' in note if case=='unowned' else 'inherited' in note,case+': unsupported source reports its specific reason')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    print('artifacts:',root)


def owned_scene(checks,case):
    root=Path(test_dir('native-imageless-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
    (mods/'mod.ini').write_text('namespace = owned_target\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nvk_hash = %08x\nvk_binding = ps-t0, 1, 5\n'%fragment+
        'if o0->width == 4\nResourceSaved = ref o0\nhandling = skip\nelif ResourceSaved->width == 4\nps-t0 = ref ResourceSaved\nendif\n',encoding='ascii')
    settings=root/'settings';fixes=settings/'ShaderFixes';fixes.mkdir(parents=True)
    shader=root/'sample.hlsl';shader.write_text('[[vk::binding(5,1)]] [[vk::combinedImageSampler]] Texture2D<float4> tex;\n[[vk::binding(5,1)]] [[vk::combinedImageSampler]] SamplerState samp;\nfloat4 main() : SV_Target0 { return tex.SampleLevel(samp,float2(0,0),0); }\n',encoding='ascii')
    compile_shader(shader,fixes,'%08x'%fragment,'ps')
    pixels=root/'pixels.raw';trigger=root/'capture.now'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output resources',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger))
    args=['--readable-swapchain','--native-target','--imageless','--api','1.3']
    if case=='owned_push':args+=['--descriptors','push']
    with Sandbox(seconds=20,threads=0,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case=='owned_swap':
                module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
                deadline=time.monotonic()+6
                while time.monotonic()<deadline:
                    status=probe.status().get('replacement',{})
                    if status.get('module',{}).get('generation',0)>0 and status.get('shader_overrides',{}).get('executed',0)>0:break
                    time.sleep(.05)
                checks.check(status.get('module',{}).get('generation',0)>0,case+': replacement module exchange completed')
            trigger.touch();deadline=time.monotonic()+6
            while not pixels.exists() and time.monotonic()<deadline:time.sleep(.05)
            status=probe.status().get('replacement',{})
        checks.check(pixels.exists() and sb.proc.poll() is None,case+': completed actual GPU pixels while process remains live')
        checks.check(colored_pixels(pixels,(255,0,255))>200,case+': later draw samples the retained owned color target')
        checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': target reference and descriptor binding execute without refusal')
        sb.proc.terminate();sb.proc.wait(timeout=30)
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)


def main():
    checks=Checks('native imageless resources');selected=set(sys.argv[1:])
    for case in ('classic','inject','absent','unowned','secondary'):
        if not selected or case in selected:scene(checks,case)
    for case in ('owned','owned_push','owned_swap'):
        if not selected or case in selected:owned_scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
