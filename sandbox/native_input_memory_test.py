"""Input-only constant references to overlapping and disjoint output storage.

The shader never reads the extra descriptor; both before/after runs are valid.
CPU cases separately cover sampled-image storage and attachment enumeration.
"""
import sys
from harness import Checks
from native_output_test import scene

def main():
 checks=Checks('native input memory ranges');selected=set(sys.argv[1:])
 for memory,case in ((1,'control'),(1,'core'),(1,'classic'),(1,'push'),(2,'core'),(2,'classic'),(2,'push'),(2,'swap')):
  if not selected or '%d-%s'%(memory,case) in selected:scene(checks,case,memory=memory,input_only=True)
 return checks.finish()

if __name__=='__main__':raise SystemExit(main())
