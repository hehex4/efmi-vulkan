"""Build the offline SMOL-V bridge from sources already installed in F:/vulkan."""
from pathlib import Path
import hashlib
import json
from build import find_vcvars, run_cl

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = Path('F:/vulkan/unity-analysis/smol-v').resolve()


if __name__ == '__main__':
    require_files = [DEPENDENCY / 'smolv.cpp', DEPENDENCY / 'smolv.h', ROOT / 'scripts/shader_smolv_bridge.cpp']
    if not all(path.is_file() for path in require_files):
        raise RuntimeError('Install smolv.cpp and smolv.h as documented in docs/shader-input-metadata.md')
    output = ROOT / 'build/shader-survey-decoder'
    output.mkdir(parents=True, exist_ok=True)
    run_cl(find_vcvars(), str(output), ['/nologo', '/std:c++17', '/EHsc', '/O2', '/LD',
        '/I' + str(DEPENDENCY), str(require_files[0]), str(require_files[2]), '/Fe:smolv_bridge.dll'])
    paths = require_files + [output / 'smolv_bridge.dll']
    evidence = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    (output / 'sources.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='ascii')
    print('Built', output / 'smolv_bridge.dll')
