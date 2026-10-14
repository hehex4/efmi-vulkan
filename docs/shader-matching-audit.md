# Original shader matching and query audit

Date:2026-10-14. Read-only XXMI revision:
a7847eae82aaac32175c9a7d199673d5c9323bcd. This is source evidence; no application
shader identity or game rendering is established by the audit.

## Matching and execution

IniHandler.cpp::ParseShaderOverrideSections owns hash parsing, duplicate policy,
merged command lists, filter_index, model, depth_filter and disable_scissor.
Keep that parser. The disable_scissor key adds a built-in CustomShader command;
model controls legacy replacement compilation rather than command filtering.

HackerContext::BeforeDraw looks up VS/HS/DS/GS/PS in that order and saves each
matching post list. Each pre list is an independent RunCommandListComplete root.
AfterDraw runs saved post lists in the same order and restores the outer input
layout afterward. Abort and pending layout edits therefore do not cross stage
roots; applied resource state does. Dispatch uses only CS and a corresponding
post root. The original call belongs between pre and post, including when no
override matches. Skip suppresses that call, not subsequent post commands.

VS/PS/CS lookups include hash0. HS/DS/GS skip their lookup when the tracked hash
is0. These details differ from checking only whether a shader handle is bound.

ProcessShaderOverride always executes its commands. A failed depth_filter
condition subsequently restores a legacy original VS/PS when such a replacement
exists. It does not suppress the command list. Compute does not use this legacy
filter path. Do not implement depth_filter as a generic predicate.

## Query identity and declaration data

CommandListOperand::process_shader_filter reads the HackerContext tracked source
shader handle, then the generation's mShaders/mShaderOverrideMap. Unbound is
negative zero; bound without a known hash or override is positive zero; an
override without explicit filter_index returns1. The explicit value may itself
be0 or negative. Preserve those distinctions.

RunCustomShaderCommand binds temporary shaders through mOrigContext1 and restores
them through that same pass-through context. It does not replace the tracked
HackerContext shader identity. Therefore vs/ps/cs queries inside nested custom
shaders still describe the original tracked shader, not the temporary executable.

The cb_type/cb_size and srv_type/srv_stride/srv_dimension members use the source
ByteCodeReader.cpp declaration cache. Constant-buffer size is the declared count
of float4 registers. Missing CB/SRV size, stride or dimension returns-1; missing
type returns NONE. Slot expressions require bounds checks before the original
unsigned cast and array access. Use the pinned reader for DXBC declarations;
Vulkan reflection alone does not prove original source slots or indexing modes.

The parser recognizes cb_mask/srv_mask, but this pinned evaluator has no cases
for them and unconditionally accesses member_args[0] on the declaration path.
These no-argument forms must remain an explicit unsupported source boundary;
do not invent mask behavior or execute the unchecked native access.

## Identity boundary and next implementation

HackerDevice.cpp hashes original DXBC with the configured FNV, embedded or
bytecode policy. A SPIR-V hash is not interchangeable. An owned host can supply
explicit source identities and declaration snapshots; application use requires
separate provenance. Unknown source facts must not become an unmatched shader.

Next: original ShaderOverride parsing, bounded stage scheduling with a real
pre/original/post boundary, tracked shader queries and declaration guards. Verify
stage/root ordering, abort/skip, duplicate policy, failure and nested shader
identity in the sandbox. Keep legacy shader substitution and ShaderRegex binary
patching explicit until their corresponding execution adapters exist.

Directory inclusion, parser coverage, compilation, owned execution and actual
application command placement are separate results. No game input is required
for this next owned implementation.
