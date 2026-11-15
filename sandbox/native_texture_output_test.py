"""Texture-only Mod output commands surround the application's retained geometry."""
import sys
from harness import Checks
from native_classic_mod_character_test import scene

checks=Checks('texture-only Mod output events');selected=set(sys.argv[1:])
for case in ('control','null','post','restore','rp2_inject'):
 if not selected or case in selected:scene(checks,case,matched=True)
raise SystemExit(checks.finish())
