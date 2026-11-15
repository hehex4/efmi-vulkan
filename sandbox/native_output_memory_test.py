"""Output storage checks with real overlapping and disjoint Vulkan suballocations.

The extra descriptor is unused by the shader: even the unguarded baseline stays
valid. It tests the conservative bound-descriptor policy, without issuing an
invalid simultaneous read/write or interpreting opaque image bytes as uniforms.
"""
import sys
from harness import Checks
from native_output_test import scene

def main():
 checks=Checks('native output memory ranges');selected=set(sys.argv[1:])
 for memory,case in ((1,'control'),(1,'core'),(1,'classic'),(1,'push'),(2,'core'),(2,'classic'),(2,'push'),(2,'swap')):
  if not selected or '%d-%s'%(memory,case) in selected:scene(checks,case,memory=memory)
 return checks.finish()

if __name__=='__main__':raise SystemExit(main())
