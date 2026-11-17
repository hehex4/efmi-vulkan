"""Original model-linked commands without an explicit native hash trigger."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
from harness import Checks,Sandbox,ROOT,configuration_root
from native_compute_test import compile_shader,read_compute_result
sys.path.insert(0,str(Path(ROOT)/'scripts'))
from build_shader_model_index import encode
from efmi_vk.probe_client import ProbeClient


def main():
    case=sys.argv[1] if len(sys.argv)>1 else 'enabled'
    if case not in ('enabled','gated','mismatch','missing'):raise ValueError('Unknown regex fixture case')
    checks=Checks('native original regex '+case)
    root=Path(ROOT)/'build/native-original-regex';mods=root/'Commands';mods.mkdir(parents=True,exist_ok=True)
    application=root/'application.hlsl'
    application.write_text('[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n'
                           '[numthreads(1,1,1)] void main() { output[0]=uint4(7,0,0,0); }\n',encoding='ascii')
    shader=compile_shader(application,root/'compiled','12345678','cs')
    code=shader.read_bytes()
    # Synthetic source evidence for this fixture, separate from the whole-asset
    # proof generator. It deliberately exercises the exact-identity host bridge.
    token=0x50040 if case=='mismatch' else 0x50050
    records=[] if case=='missing' else [(hashlib.sha256(code).digest(),len(code),token)]
    metadata=root/'shader-models.bin';metadata.write_bytes(encode(records))
    (mods/'replace.hlsl').write_text('RWStructuredBuffer<uint4> output : register(u0);\n'
                                    '[numthreads(1,1,1)] void main() { output[0]=uint4(23,0,0,0); }\n',encoding='ascii')
    (mods/'regex.ini').write_text('[Constants]\nglobal $enabled = '+('0' if case=='gated' else '1')+'\n'
        '[ShaderRegexCompute]\nshader_model = cs_5_0\nfilter_index = 7\n'
        'if $enabled && cs == 7\nhandling = skip\nrun = CustomShaderReplace\nendif\n'
        '[CustomShaderReplace]\ncs = replace.hlsl\ndispatch = 1,1,1\n',encoding='ascii')
    settings=root/'settings';settings.mkdir(exist_ok=True);result=root/'result.raw'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED='1',EFMI_VK_SHADER_MODELS=str(metadata))
    with Sandbox(threads=0,seconds=4,validation=True,env_extra=env,
                 extra_args=['--compute-shader',str(shader),'--compute-output',str(result),'--compute-capture']) as sb:
        time.sleep(1.2)
        with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{})
        sb.proc.wait(timeout=25)
        stdout=sb.output();(root/(case+'-stdout.txt')).write_text(stdout,encoding='utf-8')
        (root/(case+'-layer.log')).write_bytes(Path(sb.log_path).read_bytes())
        checks.equal(sb.proc.returncode,0,'sandbox exits normally')
    data=read_compute_result(result);value=struct.unpack_from('<I',data)[0] if len(data)==16 else None
    checks.equal(value,23 if case=='enabled' else 7,'original model and condition control real GPU replacement')
    hooks=status.get('shader_overrides',{})
    if case=='missing':checks.check('verified source shader model' in hooks.get('last_reason',''),'unknown source model refuses explicitly')
    else:checks.check(hooks.get('executed',0)>0 and not hooks.get('last_reason'),'regex entry works without a native hash override')
    checks.check('VUID-' not in stdout and 'Validation Error' not in stdout,'Vulkan and synchronization validation clean')
    (root/(case+'-status.json')).write_text(json.dumps(status,indent=2),encoding='ascii')
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
