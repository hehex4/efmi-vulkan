"""Ordered outputs and Mod draws on both sides of the retained application call."""
import sys
from harness import Checks
from native_classic_mod_test import scene

checks=Checks('Mod outputs beside original geometry');selected=set(sys.argv[1:])
for case in ('depth_beside_control','depth_beside_null','depth_beside_post','depth_beside_empty_post','depth_beside_restore',
             'depth_beside_ordered_null','depth_beside_ordered_empty','depth_beside_rp2','depth_beside_inject','depth_beside_swap'):
    if not selected or case in selected:scene(checks,case,matched=True)
raise SystemExit(checks.finish())
