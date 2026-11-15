"""Graphics ShaderOverride file publication never waits on the recording thread."""
from harness import Checks
from native_output_test import scene

checks=Checks('native graphics file readiness')
for case in ('classic','swap'):
 status=scene(checks,case,images=True)
 checks.equal(status.get('textures',{}).get('draw_waits',-1),0,case+': first file image is published without a synchronous recording wait')
 checks.equal(status.get('textures',{}).get('file_reads',-1),1,case+': pending graphics calls share one file request')
 if case=='classic':checks.check(status.get('shader_overrides',{}).get('pending',0)>0,'cold file publication is reported as pending before successful execution')
raise SystemExit(checks.finish())
