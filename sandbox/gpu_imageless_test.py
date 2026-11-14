"""Imageless analysis must use the actual Begin attachments, including exact layer ranges."""
from pathlib import Path
import sys
import gpu_target_test as target
from harness import Checks,Sandbox
def main():
 checks=Checks('GPU imageless targets');selected=set(sys.argv[1:])
 for case in ('clear','after_graphics','after_secondary','usage','discard','layers','multiview','multiview_after','unused'):
  if selected and case not in selected:continue
  def launch(**kwargs):
   kwargs['extra_args']=kwargs.get('extra_args',[])+['--imageless','--api','1.3']
   return Sandbox(**kwargs)
  target.Sandbox=launch;target.scene(checks,case)
 return checks.finish()
if __name__=='__main__':raise SystemExit(main())
