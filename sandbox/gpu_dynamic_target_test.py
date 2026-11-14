"""Actual dynamic attachments are copied only after their complete ordinary rendering pass."""
from pathlib import Path
import sys
import gpu_target_test as target
from harness import Checks,Sandbox
def main():
 checks=Checks('GPU dynamic targets');selected=set(sys.argv[1:])
 for case in ('clear','khr','after_graphics','after_secondary','usage','discard','layers','multiview','multiview_after','unused','resolve','resolve_discard','split','split_secondary'):
  if selected and case not in selected:continue
  def launch(**kwargs):
   kwargs['extra_args']=kwargs.get('extra_args',[])+['--dynamic-rendering','2' if case=='khr' else '1','--api','1.3']
   if case.startswith('split'):kwargs['extra_args']+=['--rendering-split']
   return Sandbox(**kwargs)
  mapped={'khr':'clear','split':'after_graphics','split_secondary':'after_secondary'}.get(case,case)
  target.Sandbox=launch;target.scene(checks,mapped)
 return checks.finish()
if __name__=='__main__':raise SystemExit(main())
