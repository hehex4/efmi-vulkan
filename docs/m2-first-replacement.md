# First live component replacement, 0.9.1

The user resumed the configuration pause, started Vulkan and reported the character display
(LOD0). Endfield PID 3080 loaded 0.9.1 at 2026-10-12 19:22:00 local time. Its log confirms the
component-7 package enabled at device creation. The user later confirmed voluntary exit.
The agent did not launch, stop, toggle or redeploy anything in this session.

Evidence root: `captures/m2-component-0.9.1-20261012/display-192441/` (uncommitted).

## Backend execution

Between 19:24:41.677 and 19:24:45.685, `matched` rises from 17,952 to 18,758 and `replaced`
from 17,834 to 18,640: all 806 matches in that window reach the replacement path. Pending
stays 22, unsupported 96, unreadable/exhausted zero. Present intervals are not displayed FPS.

The later `status-mesh-read.json` has matched 57,751, replaced 57,502, pending 22 and
unsupported 227, still no unreadable or exhausted cases. The last refusal combines primary
command buffer, classic render pass, direct single-instance, free set 3 and extended-bind
constraints. Which condition caused these 131 additional refusals is unresolved. `last_reason`
is only the last failure, not a histogram and not evidence for every earlier refusal.

## Saved draw data

`display-a/attempt-01` selects frame 51,570 after skipping nine empty present intervals:
178 graphics draws, 155 indexed, 45 dispatches, 28 classic passes, 18 submits and five command
buffers. All 176 manifest file fingerprints verify. Of 46 mod-matched draws, component 7 is
draw 86 and 87, pipeline 368, pass 24, command buffer 1, 108 indices and one instance each.
Its region hash is `a3fe5c47`; vertex strides are 16/8/12/16. Preflight supports the variant.

Each 256-byte instance entry and each current/previous 12 KiB bone window is equal at
record and submit. Both entries hold constant 5 `(24421, 22555, 258, 1065353216)`; original
entry CRC `f47af419`, current-window CRC `ac819d9a`, previous `a7bd8ee2`. The capture does
not expose the private replacement entries or mark individual replaced draws (LIMIT-020).
The collector's surrounding status window adds 222 matches and 222 replacements.

Full-frame replay did not run: component 10 draw 38 uses pipeline 437, whose preflight says
`layout_requires_three_complete_sets_and_one_free_slot`. The replay requires supported
preflight to identify each VS bone binding. This is separate from pipeline 368 and must not
be presented as a component-7 failure or a passed full-frame replay.

## Why visual inspection of this component is inconclusive

The mod has 40 vertices and 108 indices for component 7. Live original mesh reads after the
capture locate it in two thin strips near the eyes. XYZ positions match exactly (maximum
absolute difference zero), whereas packed fourth position words and texture coordinates
do not all match. This is a live byte comparison, not a frame snapshot or pixel comparison.
`component7-location.png` shows mesh-space context; it is not a game screenshot. The exact
surface semantic is not established from geometry alone.

The user cannot reasonably inspect such a tiny part and requests the entire mod through
general replacement logic. No appearance/animation acceptance, LOD1 replacement test or full
mod compatibility is claimed. D-033/T-027 continue the shared backend and frontend work;
do not require further microscopic visual checks before making offline progress.
