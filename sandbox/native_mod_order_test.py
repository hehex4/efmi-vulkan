"""Individual Mod draws observe successive output changes after whole-plan preflight."""
import sys
from harness import Checks
from native_classic_mod_test import scene

checks=Checks('interleaved matched Mod outputs');selected=set(sys.argv[1:])
for case in ('depth_ordered_control','depth_ordered_null','depth_ordered_restore','depth_ordered_empty',
             'depth_ordered_filter_active','depth_ordered_filter_inactive','depth_ordered_swap',
             'depth_ordered_rp2','depth_ordered_inject','mrt_ordered_null','mrt_ordered_restore'):
 if not selected or case in selected:scene(checks,case,matched=True)
raise SystemExit(checks.finish())
