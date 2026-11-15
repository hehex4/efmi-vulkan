"""Output commands inside matched Mod lists preserve their order around owned geometry."""
import sys
from harness import Checks
from native_classic_mod_test import scene

checks=Checks('matched Mod output events');selected=set(sys.argv[1:])
for case in ('depth_mesh_control','depth_mesh_null','depth_mesh_post','depth_mesh_restore',
             'depth_mesh_shader','depth_mesh_filter_active','depth_mesh_filter_inactive',
             'depth_mesh_swap','depth_mesh_rp2','depth_mesh_imageless','depth_mesh_inject',
             'mrt_mesh_null','combined_mesh_null'):
 if not selected or case in selected:scene(checks,case,matched=True)
raise SystemExit(checks.finish())
