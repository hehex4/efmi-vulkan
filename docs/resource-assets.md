# Resource asset preparation

The general frontend now has a bounded DDS metadata decoder in
frontend/assets/dds.cpp. It returns Vulkan formats and exact mip byte ranges
into the caller-owned file data. It does not decompress, recolor or duplicate
payloads. It contains no mod/component identifiers. No GPU upload or descriptor
binding is implemented by this helper, and it is not linked into the layer yet.

The supported container is one ordinary 2D texture, at most 16,384 by 16,384 and
128 MiB of source data. Explicit DX10 BC1-7 and RGBA/BGRA8 format mappings retain
linear/sRGB and signed/unsigned distinctions. Legacy DXT1/3/5, BC4/5 and exact
32-bit RGBA/BGRA masks have mappings. Typeless formats, arrays, cubes, volumes,
premultiplied/custom alpha, padded rows, inconsistent headers and extra/truncated
payloads are explicit refusals. Unknown/straight/opaque alpha headers do not
transform payload bytes. A failed decode leaves the previous output unchanged.

The original 23 hosted-frontend and 28 planner tests still pass; 15 new native
asset checks cover the sample formats, small mip block sizes, immutability and
invalid containers. Format-table entries beyond these and the actual sample
have no GPU acceptance claim. Reproduce with scripts/probe_xxmi_host.py.

scripts/check_mod_draw_plans.py now runs the production decoder on every DDS
actually bound by its selected callback scenarios, and independently compares
the returned formats, dimensions and mip ranges against the raw headers and
block sizes. All ten actual files pass: nine DX10 BC7 (linear or sRGB) and one
legacy BC5U. Each has only mip 0. Four are 8K images with 64 MiB compressed data
apiece. The 38 bound mesh/texture files total 315,328,648 bytes, about 301 MiB.
The source INI and assets remain read-only, with fingerprints in
build/xxmi-host/mod-plan-report.json.

Two extra DDS declarations in the INI name absent files, but no tested callback
ever references those declarations: its corresponding draws use other textures.
Do not treat mere declaration as a load request, fail a whole mod for an unused
declaration, or silently ignore a missing resource once it is actually selected.

The size evidence rules out reusing the component-7 package's small allocation
budget for the whole character. GPU preparation needs bounded asynchronous file
loading and upload, device format/extent/feature checks, readiness/failure states,
queue/fence ownership, and retained resources through command completion. No file
read, DDS decode, decompression or pipeline creation belongs in a draw hook. This
is a design requirement for the next integration, not a measured game overhead
or an implemented upload scheduler. No deployment/configuration change occurred.
