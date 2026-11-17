# Proven source models and original patternless shader commands

D-333, 3.4.9. This is offline asset evidence and sandbox execution, not game acceptance.

Original `HackerContext.cpp` BeforeDraw/AfterDraw runs shader lists in vertex,
hull, domain, geometry and pixel order. `ShaderRegex.cpp` 721+ matches patternless
groups by the original shader model and links their shared pre/post commands.
The installed Core enters IB overrides through its conditional
`CheckTextureOverride = ib`. Inserting matching IB lists independently bypassed
the Core's enable switch (BUG-173).

The production native call entry now parses and links those original groups.
An isolated temporary override map collects the original links by proven model;
its internal key is never exposed as a DXBC hash. Explicit native shader commands
precede regex links within each stage. Explicit filter_index takes precedence;
otherwise the first matching original regex filter applies. Repeated draws reuse
the same command identities. The old file-plan/library diagnostic entry remains
separate from the production native call entry.

DX11 provides reference-counted lifetime with deferred destruction, hazard tracking
and context order. Source-model metadata supplies none of those guarantees. The
existing prepared Vulkan transaction still owns admission, ordered GPU packets,
resource retention and image-layout handoff.

## Whole-asset source model proof

`scripts/build_shader_model_index.py` reopens every fingerprinted asset from the
earlier whole-asset survey and checks its hash before and after reading. API
pairing uses subshader/LOD, pass, hardware tier, keyword indices and requirements.
The observed version-2 combined-program header holds multiple executable payloads;
the earlier input-semantic survey read only the first one. Header flags remain
opaque. Payload ranges must be bounded, nonoverlapping and cover the complete
payload; unsupported extra payload slots refuse.

Each decoded SPIR-V entry point determines its actual stage. Each bounded DXBC
container's single SHDR/SHEX program determines its original model. Paired stage
sets must agree. An exact SPIR-V SHA-256/byte-length identity is admitted only when
every observed paired occurrence agrees on its model and no unpaired occurrence
contradicts the proof. No source name, shader name, character or Mod selects data.

The reviewed 232-asset corpus has 19,628 vertex/pixel pairs each and 1,025 compute
pairs. It admits 18,715 exact module identities with zero model conflicts; 751
unpaired-only identities remain absent. The generated cache is 748,648 bytes.
Independent spirv-val checks cover one extracted representative of each observed
stage, not every module. Full range/model parsing covers every admitted identity.

```powershell
D:/Python312/python.exe -B scripts/build_shader_model_index.py `
  --sources build/d294-survey.json --output build/shader-model-index
D:/Python312/python.exe -B scripts/check_shader_models.py
```

`ShaderModelIndex` validates framing, SHA-256 payload integrity, unique identities,
byte counts and supported model tokens before replacing its previous complete
index. Loading occurs when a configuration generation is built; draws only query
immutable metadata and a bounded retained-shader cache. The default input is
`settings/shader-models.bin`; `EFMI_VK_SHADER_MODELS` can name a test-process input.
Nothing is deployed by the generator. Cache generation/release packaging still
needs an explicit verified artifact; a missing model is never guessed from SPIR-V.

## Verification and limits

Root configuration checks pass 39, the full original interpreter 582, model-reader
checks 24 and parser boundary checks 15. Production GPU cases prove that a regex
with no explicit native shader hash can replace compute output, that its condition
can disable replacement, and that mismatched/unknown source models cannot activate
it. Installed Core checks cover real constant/rolling import command lists plus
its original enable_mods/IB gate, including actual call suppression and GPU
parameter observations. Sandbox-generated shaders use explicit synthetic model
fixtures; their facts are not represented as evidence about game shaders.

Model correspondence does not prove an original DXBC hash, declarations, assembly
or a translated patched pipeline. Patterned groups requiring those facts refuse
before command side effects. Native hash-only configurations still retain their
existing direct IB entry; fully separating that convenience from original-only
configuration semantics remains work. Prepared graphics CustomShaders, current
GPU Store boundaries and remaining queue/source/capture work also remain before
the compatibility rewrite can be called complete.
