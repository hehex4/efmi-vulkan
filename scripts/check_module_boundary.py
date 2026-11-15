"""Private implementation edits preserve the module ABI; shared protocol edits do not."""
from pathlib import Path
import io
from unittest.mock import patch
import build

source = Path(__file__).resolve().parents[1] / 'layer/src'
baseline = build.abi_stamp()
passed = failed = 0
original_open = open

def changed_stamp(name):
    target = (source / name).resolve()
    changed = target.read_text(encoding='utf-8') + '\n// ABI boundary probe.\n'
    def reader(path, *args, **kwargs):
        if Path(path).resolve() == target:
            return io.StringIO(changed)
        return original_open(path, *args, **kwargs)
    with patch('builtins.open', side_effect=reader):
        return build.abi_stamp()

for name, shared in (
    ('pipeline_variant.h', False), ('native_targets.h', False), ('native_images.h', False),
    ('state.h', True), ('resources.h', True), ('character_module.h', True),
    ('native_state.h', True),
):
    if (changed_stamp(name) != baseline) == shared:
        passed += 1
    else:
        failed += 1
        print('FAIL:', name, 'must', 'change' if shared else 'preserve', 'the ABI stamp')
print('module boundary: %d/%d checks passed' % (passed, passed + failed))
raise SystemExit(bool(failed))
