"""Matched output-only lists keep the native caller between their pre/post operations."""
import sys
from harness import Checks
from native_classic_mod_character_test import scene

checks=Checks('matched output-only application calls');selected=set(sys.argv[1:])
for case in ('control','null','post','restore','hide','rp2_inject','swap'):
 if not selected or case in selected:scene(checks,case,matched=True,output_only=True)
raise SystemExit(checks.finish())
