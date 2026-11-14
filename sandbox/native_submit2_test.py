"""Private mutable resources under actual core/KHR Submit2, with scoped signals."""
from pathlib import Path
import sys
import native_mutable_test as buffers
import native_mutable_image_test as images
from harness import Checks,Sandbox

def main():
 checks=Checks('native Submit2 resources');selected=set(sys.argv[1:])
 for label,kind,case,entry in [('buffer',buffers,'independent',1),('buffer_khr',buffers,'independent',2),
                             ('buffer_push',buffers,'push',1),('buffer_secondary',buffers,'secondary',1),
                             ('buffer_persistent',buffers,'persistent',1),('buffer_reload',buffers,'reload',1),('buffer_swap',buffers,'swap',1),
                             ('buffer_indirect',buffers,'indirect_sync2',1),('buffer_application',buffers,'application_sync2',1),
                             ('image',images,'independent',1),('image_khr',images,'independent',2),
                             ('image_secondary',images,'secondary',1),('image_sampled',images,'sampled_push',1),
                             ('image_array',images,'array',1),('image_reload',images,'reload',1)]:
  if selected and label not in selected:continue
  def launch(**kwargs):
   kwargs['extra_args']=kwargs.get('extra_args',[])+['--submit2',str(entry),'--api','1.3']
   return Sandbox(**kwargs)
  kind.Sandbox=launch
  print('Submit2 resource case:',label,flush=True);kind.scene(checks,case)
 return checks.finish()
if __name__=='__main__':raise SystemExit(main())
