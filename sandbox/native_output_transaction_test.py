"""Resource-based output proof retains earlier buffer, file and null behavior."""
from harness import Checks
from native_output_test import scene

def main():
 checks=Checks('native output resource transaction')
 for flags,cases in ((dict(constants=True),('image_mixed','file')),
                     (dict(buffers=True),('image_mixed','black','wrong_kind')),
                     (dict(images=True),('classic','image_mixed','array','mutable')),
                     (dict(null_images=True),('image_mixed','core'))):
  for case in cases:scene(checks,case,**flags)
 return checks.finish()

if __name__=='__main__':raise SystemExit(main())
