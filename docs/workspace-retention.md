# Workspace artifact retention

The user requested project-wide removal of unnecessary files on 2026-11-16.
Successful test outputs must not accumulate indefinitely by decision number.

Keep source and Git history, the active development worktree, current runtime
files and symbols, the latest verified candidate, configuration/source backups,
and original game captures or datasets still needed by an open investigation.
Keep only the active copy of reproducible asset data and its validation records.

After a check has passed and its evidence is summarized in project memory,
remove its temporary sandbox, generated fixtures, copied Mods, old baseline DLLs
and unused intermediate files. Retain concise current logs and failure evidence
that is still needed. Keep temporary isolation while a test runs; do not reuse a
directory while a process still owns its DLLs or output. This is an operating rule,
not a claim that every existing test script automatically deletes its output.

Before deleting a worktree, check tracked and untracked changes and verify that
its HEAD is merged. Before recursive cleanup, resolve each target inside this
project and reject links that leave it. Never clean the installed Mods, game,
launcher or another project. Preserve hashes of retained files for a broad cleanup.

## Cleanup on 2026-11-16 (D-297)

- Before: 114,439,179,744 bytes (106.580 GiB), 488,959 files.
- After the main cleanup: 3,598,225,827 bytes (3.351 GiB), 20,250 files and
  719 directories. Small subsequent memory/provenance records change this slightly.
- Net reduction at that measurement: 110,840,953,917 bytes (103.229 GiB).
- Removed three clean, fully merged worktrees under `.claude/worktrees`
  (39.407 GiB). Their branch commits remain in Git.
- Removed 3,146 generated-artifact targets containing 425,878 files and
  68,857,553,280 bytes. This includes completed sandbox tests, replay/Mod copies,
  old per-round fixtures and candidates, duplicate asset extraction, old backup
  binaries and compiler/linker intermediates. New release checks created some
  temporary output after the initial size measurement.
- Retained 2.149 GiB of original game captures. Kept the active shader dataset,
  exact semantic cache, current development binaries/symbols, the verified 3.1.8
  archive, root runtime configuration and one prior runtime rollback.
- All 16,553 retained source/capture/runtime/metadata files checked immediately
  after deletion matched their pre-cleanup SHA-256. Subsequent documentation
  and candidate provenance edits are recorded separately in Git.
- The retained dataset reproduces the exact 5,265-entry cache. The candidate's
  packaged cache bytes, archive SHA-256 and ZIP CRCs pass after cleanup.
- Automatic approval review rejected a further attempt to delete the root
  `dist` directory's old test executables and mismatch DLL, including a narrower
  file-only attempt. The only returned reason was `blocked by policy`; no more
  specific reason was supplied. Those ten files (about 45 MiB) remain. No approval
  bypass was attempted. Runtime deployment files were not replaced.

Ignored audit manifests are in `.worktrees/fix/build/cleanup-{plan,result,size,verification}.json`
and `cleanup-protected.json`. Historical paths in older memory entries describe
evidence from their original rounds; retired artifacts need not remain on disk.
No game was loaded, no launcher/system setting changed, and nothing was pushed.


## Current deliverable after D-300

The active candidate is build/candidate-current/EFMI-Vulkan-3.2.0.zip with its
SHA sidecar and provenance record. The prior 3.1.8 archive is the candidate
rollback. D-298 temporary-directory deletion was rejected by automatic approval
review with only blocked by policy; those test copies remain and were not
removed through another mechanism. Future cleanup must account for this exception.
