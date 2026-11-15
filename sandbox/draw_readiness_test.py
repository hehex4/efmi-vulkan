"""Cold merged, texture-only and own-mesh draws must not wait for background work."""
from harness import Checks
from native_classic_mod_character_test import scene as characters
from native_classic_mod_test import scene as own_mesh

checks = Checks('nonblocking draw readiness')
for name, status in (
    ('merged and texture-only', characters(checks, 'control', cold=True)),
    ('own mesh', own_mesh(checks, 'depth_mesh_control', matched=True)),
):
    checks.equal(status.get('record', {}).get('variant_waits', -1), 0,
                 name + ': no synchronous pipeline waits')
    checks.equal(status.get('textures', {}).get('draw_waits', -1), 0,
                 name + ': no synchronous texture waits')
raise SystemExit(checks.finish())
