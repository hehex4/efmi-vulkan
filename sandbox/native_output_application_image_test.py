"""Retained application images and ordered outputs share one resource transaction."""
import sys
from harness import Checks
from native_output_test import scene

def main():
 checks=Checks('mixed output and application images');selected=set(sys.argv[1:])
 for case in ('control','classic','before','post','core','swap'):
  if not selected or case in selected:scene(checks,case,application_images=True)
 return checks.finish()

if __name__=='__main__':raise SystemExit(main())
