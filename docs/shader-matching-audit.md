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
for them. MemberArg::GetValue returns0 for its absent expression, and the final
default returns0. Preserve that measured source behavior; do not invent a bit
mask. Correction during1.3.0: the initial audit incorrectly treated the absent
argument as unsafe without reading MemberArg::GetValue. The fixed-size argument
array and its zero default make that conclusion incorrect.

## Identity boundary and owned implementation

HackerDevice.cpp hashes original DXBC with the configured FNV, embedded or
bytecode policy. A SPIR-V hash is not interchangeable. An owned host can supply
explicit source identities and declaration snapshots; application use requires
separate provenance. Unknown source facts must not become an unmatched shader.

Candidate1.3.0 keeps original ShaderOverride parsing, schedules independent stage
roots around an owned original draw/dispatch, and evaluates original shader
queries with proven source identities and unchanged ByteCodeReader declarations.
The generation retains those facts across Present/key roots. Updates validate
the complete snapshot before publication; conflicting declarations refuse.

Targeted CPU200 checks cover original duplicate/abort/skip rules, per-stage root
state, source hash0 behavior, missing facts, declaration slots and counters.
Owned GPU9139 checks include order-sensitive native DX11 pixel references,
pre/original/post compute output123, nested temporary shader identity and failed
transaction discard. AfterDispatch retains applied input layouts; AfterDraw
restores them. Present models the source frame/counter boundary on owned work.

Legacy depth/model substitution, ShaderRegex and imported indirect calls remain
explicit refusals. These results do not establish application shader hashes or
actual Present/draw interception. General resource matching and the remaining
directory/application adapters follow without requiring game input.

Directory inclusion, parser coverage, compilation, owned execution and actual
application command placement are separate results. No game input is required
for the next owned implementation.

Full rebuilt regression1.3.0:build/next/run_all-1.3.0.out exit0/all steps; source
hashes96/9/12 match, compiler172/58 and installed corpus37/37 pass, core/sync
validation clean. Installed inputs and dist1.0.2 unchanged; no game action.

## Original ShaderRegex command linkage

The installed Core EFMI/main.ini uses a patternless VS ShaderRegex to call
CheckTextureOverride=ib for indexed draws. Its source shader_model list must be
compared to proven native shader metadata, not guessed from a Vulkan stage.

The original linker creates shared RunLinkedCommandList commands, appends them
after explicit ShaderOverride commands, and supplies filter_index only when the
explicit entry has none. It suppresses duplicate links by pointer identity.
Adapters must preserve that outcome across repeated shader bindings and keep
the linked target within the same bounded command transaction. Regex groups,
indices and hashes must be isolated between frontend generations.

link_shader_regex_groups_without_patterns defers all matching groups whenever a
matching model has a pattern-bearing group. A successful patternless link does
not prove assembly replacement or patched DXBC execution. The next implementation
must keep that boundary explicit and preserve native cache behavior without
writing outside the project. Reload can retire and replace a complete generation.

Candidate1.3.2 compiles the unchanged ShaderRegex.cpp and original PCRE2 parser.
Each linked command retains the native RunLinkedCommandList dynamic type and
shared pointer identity while entering the bounded adapter. Original duplicate
suppression and unlink/filter restoration therefore continue to work. Regex
groups, their index/hash and the per-source-model cache belong to a generation.
Native models come from the unchanged DXBC reader after container bounds checks;
a shader's Vulkan stage alone is insufficient. Required unknown/conflicting
facts refuse before root execution. Native disk shader caches remain disabled.

Targeted verification:runtime407 and owned GPU9301 checks. The unchanged installed
Core ShaderRegexEnableTextureOverrides section drives an actual replacement draw
through a proven owned IB region, with source key disable/re-enable and exact
native DX11 pixels. Repeated bindings remain idempotent; non-indexed calls bypass
the check. The unprefixed source check matches independently in pre and post;
the fixture's initial one-match expectation was corrected to two. Full rebuilt
regression passes:build/next/run_all-1.3.2.out, source hashes102/9/12 match and
core/sync validation remains clean through shutdown. This is not
application shader provenance or game command placement.

## Original assembly path audit

HackerContext::DeferredShaderReplacement first attempts patternless linkage. If
any relevant model has patterns, BinaryToAsmText calls the original Flugan
disassembler, preserving round-trippable float values and the source's optional
constant-buffer/custom-data handling. Plain D3DDisassemble output is not an
equivalent text contract for every pattern.

apply_shader_regex_groups processes sorted groups. Pattern-only
groups can link commands without changing an executable; replacement patterns,
temporary-register substitution and inserted declarations may require rebuilt
bytecode. Matching a later group can depend on earlier text changes; treating
every group as an independent search changes pinned behavior. The source fast path ignores
declarations when a group has no patterns; do not invent a patch in that path.

Native patch execution uses AssembleFluganWithSignatureParsing. A proof for
owned original DXBC can cover disassembly, original matching/substitution and
reassembly offline. Vulkan execution additionally needs a bounded translator
and an executable associated with that exact patched source. A stored original
model or a SPIR-V hash does not establish this association. Candidate1.3.3
accepts proven pattern-only matching; bytecode-changing results remain analysis
only until a verified Vulkan executable is available.
Continue this path offline before application integration.

Correction during1.3.3: the internal group object has a collection of patterns,
but this pinned parser accepts only the exact .Pattern section. It explicitly
defers numbered/multiple patterns. An initial fixture using .Pattern1 was invalid
and was removed; a negative test now requires its original parser diagnostic
before generation preparation. The earlier inference about partial failure in
multi-pattern INI groups is withdrawn. Ordered behavior between valid groups
remains relevant and is tested independently.

The native Globals constructor leaves assemble_signature_comments,
disassemble_undecipherable_custom_data and patch_cb_offsets uninitialized. The
full original Rendering loader normally supplies them before regex parsing.
The hosted wrapper now calls their original getters with the same false defaults
in both runtime and inspection hosts, before native regex hashing or disassembly.


Verified1.3.3: original named captures, temporary-register allocation, declaration
insertion, sorted dependent groups and unmatched groups have native WARP outputs
1/7/9. Source identity conflicts, missing evidence and invalid original syntax
refuse without altering explicit roots/local storage. Patterned Core execution
passes exact native pixels. Full rebuilt suite:build/next/run_all-1.3.3.out;
runtime475/resources9360, source hashes104/9/12 match, core/sync clean. No game or
deployment. Original patched DXBC still needs executable translation.
