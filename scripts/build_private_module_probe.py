"""Build a sandbox-only module with a different private PipelineVariant layout.

Run after the normal host build, with no other build running. The source header is
restored even if compilation fails. The shared ABI must remain identical naturally.
"""
from pathlib import Path
import json
import shutil
import build

root = Path(__file__).resolve().parents[1]
header = root / 'layer/src/pipeline_variant.h'
original = header.read_bytes()
stamp = build.abi_stamp()
out = root / 'build/private-module-probe'
shutil.copytree(root / 'dist', out, dirs_exist_ok=True)
newline = b'\r\n' if b'\r\n' in original else b'\n'
marker = b'class PipelineVariant {' + newline + b'public:' + newline
assert marker in original
try:
    header.write_bytes(original.replace(marker, marker +
        b'    uint64_t private_layout_probe[8] = {}; // Sandbox ABI separation probe.' + newline, 1))
    assert build.abi_stamp() == stamp, 'Private representation changed the shared ABI'
    # Keep altered-layout objects out of the normal module's incremental build.
    (root / 'layer/build/module_private_probe').mkdir(exist_ok=True)
    original_run_cl = build.run_cl
    def isolated_run_cl(vcvars, folder, args):
        return original_run_cl(vcvars, folder, [
            arg.replace('build\\module\\', 'build\\module_private_probe\\') for arg in args
        ])
    build.run_cl = isolated_run_cl
    path = build.build_module(build.find_vcvars(), build.find_vulkan_include(), str(out))
    # The Vulkan loader interprets every *.json in VK_LAYER_PATH as a manifest.
    (out / 'private-layout-probe.txt').write_text(json.dumps({
        'abi': stamp, 'private_prefix_bytes': 64, 'module': path,
        'host': str(root / 'dist/efmi_vk_layer.dll'),
        'scope': 'Sandbox module only; source header restored after build',
    }, indent=2), encoding='ascii')
    print('Private layout module:', path, 'ABI:', stamp, flush=True)
finally:
    header.write_bytes(original)
assert build.abi_stamp() == stamp
