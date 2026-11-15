"""Merged skeleton geometry and independent later draws observe ordered target events."""
import sys
from harness import Checks
from native_classic_mod_character_test import scene

checks=Checks('merged skeleton output events');selected=set(sys.argv[1:])
for case in ('control','null','post','restore','between','between_restore','rp2_inject','swap'):
    if not selected or case in selected:scene(checks,case,matched=True,merged_outputs=True)
raise SystemExit(checks.finish())
