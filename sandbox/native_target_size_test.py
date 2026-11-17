"""Original rt_width/rt_height operands in native ShaderOverride calls."""
from pathlib import Path
import json
import sys
import time
from harness import Checks, Sandbox, test_dir, configuration_root
from runtime_test import sandbox_shader_hashes, band_pixels
from efmi_vk.probe_client import ProbeClient


def scene(checks,case):
    root=Path(test_dir('native-target-size-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
    owned=case in ('owned','mip','array');size=8 if case=='mip' else -1 if case=='array' else 4
    body=('if o0->width > 0 && o0->width < 32\n$seen = rt_width == %d && rt_height == %d\nhandling = skip\n'% (size,size)+'elif ' if owned else 'if ')
    body+='rt_width == o0->width && rt_height == o0->height && rt_width > 32 && $seen == 1\nhandling = skip\nendif\n'
    (mods/'mod.ini').write_text('namespace = native_size\n[Constants]\nglobal $seen = %d\n[ShaderOverrideSize]\nvk_hash = %08x\n'%(0 if owned else 1,fragment)+body,encoding='ascii')
    settings=root/'settings';settings.mkdir();pixels=root/'pixels.raw'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native target size',EFMI_VK_TEST_NOTICES_READBACK=str(pixels))
    args=['--readable-swapchain']+(['--native-target'] if owned else [])
    if case=='mip':args.append('--capture-target-mip')
    if case=='array':args.append('--capture-target-layers')
    with Sandbox(seconds=3,threads=0,validation=True,entry='inject' if case=='inject' else 'layer',env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{})
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    checks.check(pixels.exists() and band_pixels(pixels)<30,case+': exact target-size operands skip every main draw')
    ini=status.get('configuration_generation',{}).get('ini',{})
    checks.check(ini.get('variables',{}).get('$\\native_size\\seen')==1 and ini.get('diagnostics')==0,case+': original full-resource or unsupported-view answer is explicit without operand degradation')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)


def main():
    checks=Checks('native target sizes');selected=set(sys.argv[1:])
    for case in ('classic','inject','owned','mip','array'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
