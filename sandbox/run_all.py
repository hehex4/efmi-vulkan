"""Builds layer and sandbox, then runs the whole regression suite, or the steps a change touches.

    python sandbox/run_all.py [--no-build] [--only WORDS] [--list]

--only takes words separated by commas: a step runs when its name contains one of them (any case),
or one of the groups below names it. The whole suite runs before a deployment; between, the checks
of the area a change touches (the user, 2026-10-26: the whole suite every time takes too long).
--list prints the steps and groups.

Set EFMI_VK_DIST to build and test in another directory than dist/, for example
while a running game holds the DLL in dist/.

The layer is enabled only inside the child processes the tests start. Nothing is
registered system-wide and the game is not involved.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))

STEPS = [
    ("text check", [os.path.join(ROOT, "scripts", "check_text.py")]),
    ("shader asset metadata", [os.path.join(HERE, "shader_asset_metadata_test.py")]),
    ("shader VFS metadata", [os.path.join(HERE, "shader_vfs_test.py")]),
    ("shader input index", [os.path.join(ROOT, "scripts", "check_shader_inputs.py")]),
    ("shader model index", [os.path.join(ROOT, "scripts", "check_shader_models.py")]),
    ("shader source index", [os.path.join(ROOT, "scripts", "check_shader_sources.py")]),
    ("original shader hash modes", [os.path.join(ROOT, "scripts", "check_shader_hashes.py")]),
    ("bytecode resource storage reads", [os.path.join(ROOT, "scripts", "check_configuration.py"), "--only", "bytecode-resources"]),
    ("native shader input semantics", [os.path.join(HERE, "native_vertex_semantics_test.py")]),
    ("unit tests", [os.path.join(HERE, "unit_test.py")]),
    ("original frontend host", [os.path.join(ROOT, "scripts", "probe_xxmi_host.py"), "--reuse"]),
    ("directory INI language", [os.path.join(HERE, "ini_folder_test.py")]),
    ("source-defined INI language", [os.path.join(ROOT, "scripts", "check_ini_language.py")]),
    ("installed Mods and Core syntax", [os.path.join(ROOT, "scripts", "check_ini_folder.py")]),
    ("installed INI operation census", [os.path.join(ROOT, "scripts", "ini_operation_census.py")]),
    ("original INI runtime", [os.path.join(ROOT, "scripts", "build_xxmi_runtime.py")]),
    ("original root configuration", [os.path.join(ROOT, "scripts", "check_configuration.py")]),
    ("configuration backend", [os.path.join(HERE, "configuration_backend_test.py")]),
    ("native original command entry", [os.path.join(HERE, "native_original_entry_test.py")]),
    ("configuration geometry", [os.path.join(HERE, "configuration_geometry_test.py")]),
    ("configuration IA facts", [os.path.join(HERE, "configuration_ia_test.py")]),
    ("native prepared pass insertion", [os.path.join(HERE, "native_prepared_test.py")]),
    ("native configuration transaction", [os.path.join(HERE, "native_configuration_test.py")]),
    ("native configuration graphics", [os.path.join(HERE, "native_configuration_graphics_test.py")]),
    ("native configuration graphics unknown stage", [os.path.join(HERE, "native_configuration_graphics_test.py"), "missing_stage"]),
    ("native configuration graphics unknown state", [os.path.join(HERE, "native_configuration_graphics_test.py"), "missing_state"]),
    ("native configuration graphics sampler", [os.path.join(HERE, "native_configuration_graphics_test.py"), "sampler"]),
    ("native configuration graphics unknown sampler", [os.path.join(HERE, "native_configuration_graphics_test.py"), "missing_sampler"]),
    ("native cached source queries", [os.path.join(HERE, "native_cached_source_test.py")]),
    ("native source declarations", [os.path.join(HERE, "native_source_declarations_test.py")]),
    ("native source declarations missing", [os.path.join(HERE, "native_source_declarations_test.py"), "missing"]),
    ("native source declarations words", [os.path.join(HERE, "native_source_declarations_test.py"), "words"]),
    ("native exact source declarations", [os.path.join(HERE, "native_source_declarations_test.py"), "exact"]),
    ("native exact source unknown identity", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_unknown"]),
    ("native exact source invalid slot", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_slot"]),
    ("native inherited compute", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit"]),
    ("native inherited compute unknown", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_unknown"]),
    ("native inherited compute nested", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_nested"]),
    ("original source hash entry fnv", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_fnv"]),
    ("original source hash entry embedded", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_embedded"]),
    ("original source hash entry bytecode", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_bytecode"]),
    ("original source hash entry unknown", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_unknown"]),
    ("original source hash entry mismatched slots", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_slots"]),
    ("original executable compute regex", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex"]),
    ("original executable compute regex skip", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_skip"]),
    ("original executable compute regex prepost", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_prepost"]),
    ("original executable compute regex unmatched", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_nomatch"]),
    ("original indirect compute regex", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_indirect"]),
    ("original indirect compute regex skip", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_indirect_skip"]),
    ("original indirect compute regex prepost", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_indirect_prepost"]),
    ("original indirect compute regex unmatched", [os.path.join(HERE, "native_source_declarations_test.py"), "exact_inherit_source_regex_indirect_nomatch"]),
    ("native original Core constant", [os.path.join(HERE, "native_core_execution_test.py")]),
    ("native original Core rolling", [os.path.join(HERE, "native_core_execution_test.py"), "rolling"]),
    ("native original Core IB enabled", [os.path.join(HERE, "native_core_execution_test.py"), "gate_enabled"]),
    ("native original Core IB disabled", [os.path.join(HERE, "native_core_execution_test.py"), "gate_disabled"]),
    ("native original regex enabled", [os.path.join(HERE, "native_regex_test.py")]),
    ("native original regex gated", [os.path.join(HERE, "native_regex_test.py"), "gated"]),
    ("native original regex model mismatch", [os.path.join(HERE, "native_regex_test.py"), "mismatch"]),
    ("native original regex missing model", [os.path.join(HERE, "native_regex_test.py"), "missing"]),
    ("prepared GPU commands", [os.path.join(ROOT, "scripts", "check_configuration.py"), "--only", "prepared"]),
    ("owned and prepared graphics", [os.path.join(ROOT, "scripts", "check_configuration.py"), "--only", "graphics"]),
    ("owned image and parameter commands", [os.path.join(ROOT, "scripts", "check_configuration.py"), "--no-build", "--only", "owned-images"]),
    # D-129: every writable global of the runtime is per generation, per thread, the load's, locked or
    # classified as safe to share; run after the runtime's build above.
    ("INI runtime globals classified", [os.path.join(ROOT, "scripts", "check_engine_globals.py")]),
    # Cause C of docs/design-review-1.9.md (D-130): a texture's first use waits no longer than the
    # original's load of it (DirectXTK on D3D11), the installed Mods' largest maps from the file cache.
    ("texture first-use loads against the original", [os.path.join(ROOT, "scripts", "bench_texture_load.py"), "--check"]),
    ("source-defined INI evaluation", [os.path.join(ROOT, "scripts", "check_ini_evaluation.py")]),
    ("INI execution matrix", [os.path.join(ROOT, "scripts", "check_ini_execution_matrix.py")]),
    ("DX11 registers from SPIR-V", [os.path.join(ROOT, "scripts", "check_source_slots.py")]),
    ("DX11 declarations from SPIR-V", [os.path.join(ROOT, "scripts", "check_source_declarations.py")]),
    # D-170: what `ps-tN->Format`, `->Width`, `->Height`, `->Mips` and `->Array` read of the game's texture,
    # against the DX11 side of every paired draw of the catalogue.
    ("DX11 texture properties from Vulkan images", [os.path.join(ROOT, "scripts", "check_texture_properties.py")]),
    ("Core facts from SPIR-V", [os.path.join(ROOT, "scripts", "check_core_facts.py")]),
    ("RabbitFX texture roles", [os.path.join(ROOT, "scripts", "check_texture_roles.py")]),
    ("installed root configuration", [os.path.join(ROOT, "scripts", "check_configuration.py"), "--only", "gpu", "--installation", "D:/XXMI-Launcher-Portable-v2.1.6/EFMI"]),
    # --mod-first: no matched draw shows the game's mesh, not even while a new object is identified or
    # an EFMI Mod registers with the Core's API (the user's bar; held instead, 2026-10-26).
    ("captured frame replayed with the Mods folder", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                      r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk03", "--frames", "300", "--check",
                                                      "--mod-first"]),
    # Si Lunabelle's optional guard runs the Core's spatial identification itself (D-086). A copy of that
    # Mod alone, so the step holds whichever Si Mod the user has enabled.
    ("captured frame with a Mod's own identification", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                       r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk04", "--frames", "120", "--check",
                                                       "--only-mod", "Si Lunabelle 1.4"]),
    # Si NightveilTrancer drives shape keys from its Present (a compute pass writes the mesh's positions,
    # which its draws bind by reference as vb0) and guards every draw with `if rt_width != 0`, which the
    # original evaluates again in the post phase's fresh state (BUG-053). Every matched draw must plan.
    ("captured frame with Present compute and owned vertex streams", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                                     r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk04", "--frames", "120", "--check",
                                                                     "--no-refusal", "--only-mod", "Si\uff1aNightveilTrancer & Gui"]),
    # The same Mod's parts read bones of several components; on two queues a recording draws parts whose
    # other components another recording imports: every recording dispatches all of them (BUG-054), and
    # the pictures with the bones on the GPU equal those with the CPU's master.
    ("captured frame with bones across recordings", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                     r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk04", "--frames", "120", "--check",
                                                     "--two-queues", "--bones-identical", "--only-mod", "Si\uff1aNightveilTrancer & Gui"]),
    # The game keeps a frame's bone data valid only until that frame completed, then reuses the region
    # (BUG-060): with two copies of every bone buffer, the one of the frame before zeroed before a frame is
    # submitted, the GPU import must read the game's data only in the batch of the recording that drew
    # the component, and its pictures equal the CPU import's (D-127). Typhoeus' parts read bones of
    # several components, which the split passes put in other recordings.
    ("captured frame with the game's bone regions reused", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                           r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk06", "--frames", "60", "--check",
                                                           "--split-passes", "--rolling-bones", "--bones-identical",
                                                           "--only-mod", "Typhoeus\uff1aNya Tech & GUI"]),
    # SiModGB (BUG-064): an index buffer declared with `stride = 6` beside its format, typed vertex buffers,
    # and `if ps-tN == <filter_index>` to find which slot holds one of its textures, all as the original
    # takes them; every matched draw plans.
    ("captured frame with texture filter queries", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                    r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk04", "--frames", "90", "--check",
                                                    "--no-refusal", "--only-mod", "SiModGB"]),
    # Tangtang's draw lists ask the game's shaders `vs->srv_stride(0) == 16`, `ps != 1718.2`: the
    # declarations come from the pipeline's SPIR-V (D-124) and every matched draw plans.
    ("captured frame with shader declaration queries", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                        r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk02", "--frames", "120", "--check",
                                                        "--no-refusal", "--only-mod", "Tangtang - Uniform NSFW"]),
    # A full card (the game leaves almost no device memory free): with pageable device memory the Mod
    # still activates at its first match, as 3DMigoto creates resources whatever the free memory (D-122).
    ("captured frame on a full card", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                       r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk04", "--frames", "120", "--check",
                                       "--reserve-mb", "1000000", "--only-mod", "Si\uff1aNightveilTrancer & Gui"]),
    # The game's two graphics queues (D-119): a frame's passes on two command buffers over both queues,
    # the queues swapping every frame, three frames in flight; the merged skeleton's import on the GPU
    # (D-117) must give the CPU import's pictures and complete on both queues.
    ("captured frame on two queues, bones on the GPU and the CPU", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                                   r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk03", "--frames", "120", "--check",
                                                                   "--two-queues", "--bones-identical"]),
    # ardelia calls the EFMITTL library's translucent draw (D-112): the library is loaded, every plan runs, its
    # shader groups run as the host reads them off SPIR-V (D-171) with the textures' properties it asks (D-170),
    # and with the game's copy pass after the forward pass its calls are captured and composited (D-113). The
    # step used Yvonne until the user's update of 2026-11-10 left out Yvonne's translucent call (BUG-088).
    ("captured frame with a library call", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                            r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk11", "--frames", "120", "--check", "--no-refusal",
                                            "--copy-passes", "--expect-translucency", "--expect-shadows"]),
    # Yvonne calls the library's texture slots only (SetTextures, D-170, D-171): every plan runs.
    ("captured frame with a library's texture slots", [os.path.join(ROOT, "scripts", "replay_capture.py"), "--bundle",
                                                       r"F:\efmi vulkan\captures\catalogue-20261019\vulkan\vk01", "--frames", "120", "--check",
                                                       "--no-refusal", "--copy-passes"]),
    ("EFMITTL hook rules on the paired catalogue", [os.path.join(ROOT, "scripts", "check_shader_hooks.py")]),
    ("original INI GPU resources", [os.path.join(ROOT, "scripts", "check_resource_commands.py")]),
    # D-150: the original as the judge: INI cases under the pinned XXMI 3DMigoto in a D3D11 program and under the
    # owned GPU runtime, their persistent variables compared (after the GPU resource runtime is built).
    ("the original as the judge (XXMI DX11)", [os.path.join(ROOT, "scripts", "check_dx11_judge.py")]),
    # P3-1 of docs/architecture-review-2.1.md: the judge's program draws, the original runs the cases'
    # [TextureOverride] sections matched by the draws' index hashes, and the picture, the primitives and
    # the variables are compared with what the layer's plans of the same lists mean.
    ("the original as the judge of replaced draws (XXMI DX11)", [os.path.join(ROOT, "scripts", "check_dx11_judge_draws.py")]),
    # One working replaced mesh and, one at a time, the lines of sandbox/judge_draw_sweep.json in front of its
    # draw: no single line may make a Mod vanish. The record says which lines differ from the original and why;
    # the step fails on a line that agreed and differs now, and on a recorded difference that agrees now.
    ("one line at a time under the original (XXMI DX11)", [os.path.join(ROOT, "scripts", "check_dx11_judge_draws.py"), "--sweep"]),
    ("EFMITTL owned transparency", [os.path.join(ROOT, "scripts", "check_efmittl.py")]),
    ("native translucency depth history", [os.path.join(HERE, "native_translucency_depth_test.py")]),
    ("native depth history without null descriptors", [os.path.join(HERE, "native_capture_no_null_test.py")]),
    ("native opacity shadow outputs", [os.path.join(HERE, "native_shadow_output_test.py")]),
    ("native own mesh opacity shadows", [os.path.join(HERE, "native_own_shadow_test.py")]),
    ("native own mesh forward captures", [os.path.join(HERE, "native_own_capture_test.py")]),
    ("native merged opacity values", [os.path.join(HERE, "native_merged_opacity_test.py")]),
    ("native capture attachment snapshots", [os.path.join(HERE, "native_capture_snapshot_test.py")]),
    ("native deferred capture outputs", [os.path.join(HERE, "native_capture_output_test.py")]),
    ("original HLSL Vulkan compiler", [os.path.join(ROOT, "scripts", "check_shader_compiler.py")]),
    ("installed Mods and Core HLSL", [os.path.join(ROOT, "scripts", "check_hlsl_folder.py")]),
    ("directory HLSL evidence", [os.path.join(HERE, "hlsl_folder_test.py")]),
    ("original panel HLSL compilation", [os.path.join(ROOT, "scripts", "check_panel_hlsl.py")]),
    ("native tracker tests", [os.path.join(HERE, "native_test.py")]),
    ("CPU skeleton import", [os.path.join(HERE, "bone_test.py")]),
    ("pipeline recipes and variants", [os.path.join(HERE, "variant_test.py")]),
    ("captured shader rewrite corpus", [os.path.join(HERE, "variant_test.py"), "--corpus"]),
    ("legacy backend retirement", [os.path.join(HERE, "legacy_backend_retirement_test.py")]),
    ("historical package inspection", [os.path.join(HERE, "character_test.py")]),
    ("live INI rendering", [os.path.join(HERE, "runtime_test.py")]),
    ("native game shader replacement", [os.path.join(HERE, "native_shader_test.py")]),
    ("native graphics shader readiness", [os.path.join(HERE, "native_shader_test.py"), "pixel", "vertex", "empty", "f10:reload"]),
    ("native graphics file readiness", [os.path.join(HERE, "native_file_readiness_test.py")]),
    ("actual queue overlap", [os.path.join(HERE, "queue_overlap_test.py")]),
    ("native compute first use", [os.path.join(HERE, "native_compute_test.py"), "direct", "indirect", "reload"]),
    ("native compute uniform aliases", [os.path.join(HERE, "native_compute_uniform_test.py")]),
    ("native Submit2 resources", [os.path.join(HERE, "native_submit2_test.py")]),
    ("native dynamic depth", [os.path.join(HERE, "native_dynamic_depth_test.py")]),
    ("native classic attachments", [os.path.join(HERE, "native_classic_attachment_test.py")]),
    ("native classic null", [os.path.join(HERE, "native_classic_null_test.py")]),
    ("merged skeleton output events", [os.path.join(HERE, "native_merged_output_test.py")]),
    ("Mod outputs beside original geometry", [os.path.join(HERE, "native_mod_beside_output_test.py")]),
    ("native output phases", [os.path.join(HERE, "native_classic_null_test.py"), "depth_pre", "depth_post", "depth_restore"]),
    ("native classic Mod geometry", [os.path.join(HERE, "native_classic_mod_test.py")]),
    ("native classic Mod character", [os.path.join(HERE, "native_classic_mod_character_test.py")]),
    ("nonblocking draw readiness", [os.path.join(HERE, "draw_readiness_test.py")]),
    ("matched native descriptors", [os.path.join(HERE, "native_matched_descriptor_test.py")]),
    ("application texture rebinding", [os.path.join(HERE, "character_texture_rebind_test.py")]),
    ("module ABI boundary", [os.path.join(ROOT, "scripts", "check_module_boundary.py")]),
    ("native module classic scope", [os.path.join(HERE, "native_classic_null_test.py"), "color_swap", "depth_pinkclear"]),
    ("native module image scope", [os.path.join(HERE, "native_application_image_test.py"), "graphics_swap", "layout"]),
    ("native module uniform scope", [os.path.join(HERE, "native_uniform_persistence_test.py"), "swap", "classic"]),
    ("native module character scope", [os.path.join(HERE, "native_classic_mod_test.py"), "depth_mesh_swap", "depth_mesh_control"]),
    ("native module merged scope", [os.path.join(HERE, "native_merged_output_test.py"), "swap"]),
    ("matched Mod output events", [os.path.join(HERE, "native_mod_output_test.py")]),
    ("interleaved Mod output events", [os.path.join(HERE, "native_mod_order_test.py")]),
    ("texture-only Mod output events", [os.path.join(HERE, "native_texture_output_test.py")]),
    ("matched output-only application calls", [os.path.join(HERE, "native_mod_call_output_test.py")]),
    ("native classic outputs", [os.path.join(HERE, "native_classic_output_test.py")]),
    ("native depth destinations", [os.path.join(HERE, "native_depth_destination_test.py")]),
    ("native depth only", [os.path.join(HERE, "native_depth_only_test.py")]),
    ("native depth null", [os.path.join(HERE, "native_depth_null_test.py")]),
    ("native preserved stencil", [os.path.join(HERE, "native_preserved_stencil_test.py")]),
    ("native MRT outputs", [os.path.join(HERE, "native_mrt_output_test.py")]),
    ("native preserved depth", [os.path.join(HERE, "native_preserved_depth_test.py")]),
    ("native absent outputs", [os.path.join(HERE, "native_absent_output_test.py")]),
    ("native null outputs", [os.path.join(HERE, "native_output_null_test.py")]),
    ("native output destinations", [os.path.join(HERE, "native_output_test.py")]),
    ("native output memory ranges", [os.path.join(HERE, "native_output_memory_test.py")]),
    ("mixed output and application images", [os.path.join(HERE, "native_output_application_image_test.py")]),
    ("native output resource transaction", [os.path.join(HERE, "native_output_transaction_test.py")]),
    ("native ordered image phases", [os.path.join(HERE, "native_image_phase_test.py")]),
    ("native input memory ranges", [os.path.join(HERE, "native_input_memory_test.py")]),
    ("native uniform persistence", [os.path.join(HERE, "native_uniform_persistence_test.py")]),
    ("mixed output and constant-buffer bindings", [os.path.join(HERE, "native_output_constant_test.py")]),
    ("mixed output and buffer SRV bindings", [os.path.join(HERE, "native_output_buffer_test.py")]),
    ("mixed output and file image bindings", [os.path.join(HERE, "native_output_image_test.py")]),
    ("mixed output and null image bindings", [os.path.join(HERE, "native_output_null_image_test.py")]),
    ("native dynamic targets", [os.path.join(HERE, "native_dynamic_target_test.py")]),
    ("native rendering boundaries", [os.path.join(HERE, "native_rendering_boundary_test.py")]),
    ("native dynamic rendering", [os.path.join(HERE, "native_rendering_shader_test.py")]),
    ("native dynamic shaders", [os.path.join(HERE, "native_dynamic_shader_test.py")]),
    ("native extended raster", [os.path.join(HERE, "native_extended_raster_test.py")]),
    ("native primitive restart", [os.path.join(HERE, "native_primitive_restart_test.py")]),
    ("native graphics stages", [os.path.join(HERE, "native_graphics_stages_test.py")]),
    ("native imageless resources", [os.path.join(HERE, "native_imageless_test.py")]),
    ("native target sizes", [os.path.join(HERE, "native_target_size_test.py")]),
    ("native target resources", [os.path.join(HERE, "native_target_test.py")]),
    ("native resource slots", [os.path.join(HERE, "native_resource_test.py")]),
    ("native call execution", [os.path.join(HERE, "native_execution_test.py")]),
    ("native descriptor bindings", [os.path.join(HERE, "native_binding_test.py")]),
    ("analysis controls", [os.path.join(HERE, "analysis_control_test.py")]),
    ("analysis export formats", [os.path.join(HERE, "analysis_export_test.py")]),
    ("analysis resource selection", [os.path.join(HERE, "analysis_selection_test.py")]),
    ("GPU frame analysis", [os.path.join(HERE, "gpu_analysis_test.py")]),
    ("GPU geometry analysis", [os.path.join(HERE, "gpu_geometry_test.py")]),
    ("GPU dynamic geometry", [os.path.join(HERE, "gpu_dynamic_geometry_test.py")]),
    ("GPU dynamic vertex input", [os.path.join(HERE, "gpu_dynamic_input_test.py")]),
    ("GPU primitive restart", [os.path.join(HERE, "gpu_primitive_restart_test.py")]),
    ("GPU indirect analysis", [os.path.join(HERE, "gpu_indirect_test.py")]),
    ("GPU image shapes", [os.path.join(HERE, "gpu_image_shape_test.py")]),
    ("GPU dynamic target analysis", [os.path.join(HERE, "gpu_dynamic_target_test.py")]),
    ("GPU imageless target analysis", [os.path.join(HERE, "gpu_imageless_test.py")]),
    ("GPU target analysis", [os.path.join(HERE, "gpu_target_test.py")]),
    ("native image shapes", [os.path.join(HERE, "native_image_shape_test.py")]),
    ("image upload shapes", [os.path.join(ROOT, "scripts", "check_image_upload.py")]),
    ("native image bindings", [os.path.join(HERE, "native_image_test.py")]),
    ("native application shapes", [os.path.join(HERE, "native_application_shape_test.py")]),
    ("native storage images", [os.path.join(HERE, "native_storage_image_test.py")]),
    ("native application images", [os.path.join(HERE, "native_application_image_test.py")]),
    ("native graphics uniforms", [os.path.join(HERE, "native_uniform_test.py")]),
    ("mutable native file buffers", [os.path.join(HERE, "native_mutable_test.py")]),
    ("mutable native file images", [os.path.join(HERE, "native_mutable_image_test.py")]),
    ("native file bindings", [os.path.join(HERE, "native_file_test.py")]),
    ("native compute shaders", [os.path.join(HERE, "native_compute_test.py")]),
    ("native empty inputs", [os.path.join(HERE, "native_input_test.py")]),
    ("persistent hunting overlay", [os.path.join(HERE, "hunting_overlay_test.py")]),
    ("native hunting shader modes", [os.path.join(HERE, "hunting_shader_test.py")]),
    ("owned Present on a swap chain", [os.path.join(HERE, "owned_present_test.py")]),
    ("probe end to end", [os.path.join(HERE, "probe_test.py")]),
    ("modes", [os.path.join(HERE, "modes_test.py")]),
    ("budgets and reduced tracking", [os.path.join(HERE, "limits_test.py")]),
    ("many-draw timing", [os.path.join(HERE, "timing_test.py")]),
    ("Vulkan validation", [os.path.join(HERE, "validation_test.py")]),
    ("MCP end to end", [os.path.join(HERE, "mcp_test.py")]),
    ("probe evidence", [os.path.join(HERE, "evidence_test.py")]),
]


# Steps by the area of the layer they check, for --only.
GROUPS = {
    "backend-retirement": ["legacy backend retirement", "module ABI boundary", "native module", "matched native descriptors", "application texture rebinding", "native tracker", "Vulkan validation", "text check"],
    "package-validation": ["historical package inspection", "application texture rebinding", "native tracker", "Vulkan validation", "text check"],
    "native-graphics-readiness": ["native graphics shader readiness", "native graphics file readiness", "native compute first use", "nonblocking draw readiness", "native file bindings", "native tracker", "Vulkan validation", "text check"],
    "queue-overlap": ["actual queue overlap", "text check"],
    "semaphore-lifetime": ["actual queue overlap", "native graphics file readiness", "native tracker", "two queues, bones", "bone regions reused", "probe end to end", "Vulkan validation", "text check"],
    "signal-submission": ["actual queue overlap", "native graphics file readiness", "native tracker", "two queues, bones", "bone regions reused", "probe end to end", "Vulkan validation", "text check"],
    "acquisition-history": ["actual queue overlap", "native graphics file readiness", "native tracker", "two queues, bones", "bone regions reused", "probe end to end", "Vulkan validation", "text check"],
    "shader-asset-metadata": ["shader asset metadata", "shader VFS metadata", "text check"],
    "shader-input-index": ["shader input index", "shader asset metadata", "shader VFS metadata", "original frontend host", "text check"],
    "input-semantics": ["native shader input semantics", "shader input index", "original frontend host", "original INI runtime", "historical package inspection", "nonblocking draw readiness", "native merged opacity values", "native own mesh forward captures", "native tracker", "Vulkan validation", "text check"],
    "texture-rebinding": ["application texture rebinding", "matched native descriptors", "nonblocking draw readiness", "native own mesh", "native merged opacity values", "texture-only Mod output events", "native application images", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check", "source-defined INI evaluation", "INI execution matrix"],
    "module-boundary": ["module ABI boundary", "native module", "native tracker", "Vulkan validation", "text check"],
    "matched-descriptors": ["matched native descriptors", "nonblocking draw readiness", "native own mesh", "native merged opacity values", "Mod outputs beside original geometry", "texture-only Mod output events", "native call execution", "native descriptor bindings", "native graphics uniforms", "native file bindings", "original INI GPU resources", "native tracker", "Vulkan validation", "text check", "source-defined INI evaluation", "INI execution matrix"],
    "draw-readiness": ["nonblocking draw readiness", "Mod outputs beside original geometry", "texture-only Mod output events", "native own mesh", "native merged opacity values", "native tracker", "Vulkan validation", "text check"],
    "capture-output": ["native deferred capture outputs", "native capture attachment snapshots", "native opacity shadow outputs", "merged skeleton output events", "original INI runtime", "native tracker", "Vulkan validation", "text check"],
    "capture-snapshot": ["native capture attachment snapshots", "native translucency depth history", "native opacity shadow outputs", "native tracker", "Vulkan validation", "text check"],
    "shadow-output": ["native opacity shadow outputs", "merged skeleton output events", "original INI runtime", "native tracker", "Vulkan validation", "text check"],
    "own-shadow": ["native own mesh opacity shadows", "native merged opacity values", "native opacity shadow outputs", "original HLSL Vulkan compiler", "matched Mod output events", "native tracker", "Vulkan validation", "text check"],
    "own-capture": ["native own mesh forward captures", "native deferred capture outputs", "native own mesh opacity shadows", "matched Mod output events", "native tracker", "Vulkan validation", "text check"],
    "capture-no-null": ["native depth history without null descriptors", "native translucency depth history", "native capture attachment snapshots", "native tracker", "Vulkan validation", "text check"],
    "hunting-library": ["original frontend host", "original INI runtime", "captured frame with a library call", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "merged-output-tracking": ["merged skeleton output events", "native translucency depth history", "native tracker", "Vulkan validation", "text check"],
    "merged-output": ["merged skeleton output events", "Mod outputs beside original geometry", "texture-only Mod output events", "native translucency depth history", "native tracker", "Vulkan validation", "text check"],
    "mod-output-beside": ["Mod outputs beside original geometry", "interleaved Mod output events", "native classic Mod character", "native tracker", "Vulkan validation", "text check"],
    "hunting-mods": ["original frontend host", "original INI runtime", "native hunting", "matched output-only application calls", "native tracker", "Vulkan validation", "text check"],
    "hunting-resources": ["native hunting", "native call execution", "native output phases", "native tracker", "Vulkan validation", "text check"],
    "native-classic-attachments": ["native classic attachments", "native classic outputs", "native MRT outputs", "native tracker", "Vulkan validation", "text check"],
    "native-classic-null": ["native classic null", "native classic attachments", "native classic outputs", "native depth null", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "native-classic-mod": ["native classic Mod", "pipeline recipes and variants", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "mod-output": ["matched Mod output events", "native classic Mod", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "mod-output-order": ["interleaved Mod output events", "matched Mod output events", "native tracker", "Vulkan validation", "text check"],
    "texture-output": ["texture-only Mod output events", "native classic Mod character", "native tracker", "Vulkan validation", "text check"],
    "mod-output-call": ["matched output-only application calls", "texture-only Mod output events", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "native-mod-depth": ["native classic Mod", "native depth null", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "native-classic-outputs": ["native classic outputs", "native output destinations", "native depth destinations", "GPU imageless target analysis", "native tracker", "Vulkan validation", "text check"],
    "native-depth-destinations": ["native depth destinations", "native depth only", "native depth null", "native MRT outputs", "native tracker", "Vulkan validation", "text check"],
    "native-depth-only": ["native depth only", "native depth null", "native MRT outputs", "native tracker", "Vulkan validation", "text check"],
    "native-depth-null": ["native depth null", "native preserved stencil", "native null outputs", "native dynamic depth", "native tracker", "Vulkan validation", "text check"],
    "native-output-stencil": ["native preserved stencil", "native MRT outputs", "native preserved depth", "native dynamic depth", "native tracker", "Vulkan validation", "text check"],
    "native-output-mrt": ["native MRT outputs", "native preserved depth", "native null outputs", "native output destinations", "native absent outputs", "native tracker", "Vulkan validation", "text check"],
    "native-output-depth": ["native preserved depth", "native null outputs", "native output destinations", "native dynamic depth", "native dynamic targets", "native tracker", "Vulkan validation", "text check"],
    "native-absent-outputs": ["native absent outputs", "native null outputs", "native output destinations", "native descriptor bindings", "native call execution", "native tracker", "Vulkan validation", "text check"],
    "native-output-null": ["native null outputs", "native output destinations", "native tracker", "GPU dynamic target analysis", "Vulkan validation", "text check"],
    "native-output-guards": ["native output destinations", "native tracker", "Vulkan validation", "text check"],
    "native-output-constants": ["mixed output and constant-buffer bindings", "native output destinations", "native graphics uniforms", "native tracker", "Vulkan validation", "text check"],
    "native-output-buffers": ["mixed output and buffer SRV bindings", "mixed output and constant-buffer bindings", "native tracker", "Vulkan validation", "text check"],
    "native-output-images": ["mixed output and file image bindings", "native image bindings", "native tracker", "Vulkan validation", "text check"],
    "native-output-null-images": ["mixed output and null image bindings", "native image bindings", "native tracker", "Vulkan validation", "text check"],
    "native-output-memory": ["native output memory ranges", "native target resources", "native tracker", "Vulkan validation", "text check"],
    "native-output-transaction": ["mixed output and application images", "native output resource transaction", "native output memory ranges", "native application images", "native uniform persistence", "matched native descriptors", "native tracker", "Vulkan validation", "text check"],
    "native-image-phases": ["native ordered image phases", "mixed output and application images", "native output resource transaction", "native output memory ranges", "native application images", "native uniform persistence", "matched native descriptors", "native tracker", "Vulkan validation", "text check"],
    "native-input-memory": ["native input memory ranges", "native application images", "native graphics uniforms", "native tracker", "Vulkan validation", "text check"],
    "native-uniform-persistence": ["native uniform persistence", "native application images", "native graphics uniforms", "native tracker", "Vulkan validation", "text check"],
    "native-outputs": ["native output destinations", "native target resources", "native dynamic targets", "native rendering boundaries", "native descriptor bindings", "GPU dynamic target analysis", "native tracker", "Vulkan validation", "text check"],
    "gpu-dynamic-targets": ["GPU dynamic target analysis", "GPU target analysis", "native dynamic targets", "native rendering boundaries", "native tracker", "Vulkan validation", "text check"],
    "gpu-imageless": ["GPU imageless target analysis", "GPU target analysis", "native imageless resources", "native tracker", "Vulkan validation", "text check"],
    "native-compute-order": ["native compute uniform aliases", "native Submit2 resources", "native tracker", "Vulkan validation", "text check"],
    "native-submit2": ["native compute uniform aliases", "native Submit2 resources", "mutable native file buffers", "mutable native file images", "native call execution", "native descriptor bindings", "native graphics uniforms", "native rendering boundaries", "native tracker", "Vulkan validation", "text check"],
    "native-depth": ["native dynamic depth", "native game shader", "native dynamic rendering", "native hunting", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "native-dynamic-targets": ["native dynamic targets", "native target resources", "native rendering boundaries", "native application images", "native tracker", "Vulkan validation", "text check"],
    "native-rendering-boundaries": ["native rendering boundaries", "native graphics uniforms", "native dynamic rendering", "native application images", "native tracker", "Vulkan validation", "text check"],
    "native-rendering": ["native dynamic rendering", "native game shader", "native dynamic shaders", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "native-imageless": ["native imageless resources", "native target sizes", "native target resources", "native application images", "native graphics uniforms", "GPU target analysis", "native tracker", "Vulkan validation", "text check"],
    "native-target-size": ["native target sizes", "native target resources", "native game shader", "native tracker", "Vulkan validation", "text check"],
    "native-targets": ["native target resources", "native resource slots", "native application images", "GPU target analysis", "native tracker", "Vulkan validation", "text check"],
    "primitive-restart": ["GPU primitive restart", "native primitive restart", "GPU dynamic geometry", "GPU dynamic vertex input", "GPU indirect analysis", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "native-raster": ["native extended raster", "native dynamic shaders", "native game shader", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "native-dynamic-shaders": ["native dynamic shaders", "native game shader", "native hunting", "pipeline recipes and variants", "native tracker", "Vulkan validation", "text check"],
    "bones": ["bones across recordings", "bone regions reused", "two queues, bones", "CPU skeleton import", "captured shader rewrite corpus"],
    "shader-corpus": ["captured shader rewrite corpus"],
    "replay": ["captured frame"],
    "replay-readback": ["captured frame with a library call", "captured frame on two queues, bones on the GPU and the CPU", "text check"],
    "translucency-depth": ["native translucency depth history", "EFMITTL owned transparency", "native tracker", "Vulkan validation", "text check"],
    "textures": ["texture first-use", "live INI rendering", "full card", "Mods folder"],
    "submission": ["two queues, bones", "bone regions reused", "probe end to end", "Vulkan validation", "live INI rendering"],
    "ini": ["INI", "frontend host", "declarations", "registers", "Core facts", "texture roles", "texture properties", "offline plans",
            "as the judge", "one line at a time"],
    "owned": ["owned", "GPU resources", "HLSL", "Present compute", "as the judge"],
    "native-shaders": ["native game shader", "native graphics stages", "native compute", "native empty inputs", "native hunting", "native tracker", "pipeline recipes and variants", "live INI rendering", "Vulkan validation"],
    "native-input": ["native empty inputs", "live INI rendering", "text check"],
    "native-slots": ["native resource slots", "live INI rendering", "native tracker", "Vulkan validation", "text check"],
    "native-calls": ["native call execution", "native compute", "native tracker", "Vulkan validation", "text check"],
    "native-bindings": ["native descriptor bindings", "native resource slots", "native call execution", "native tracker", "Vulkan validation", "text check"],
    "analysis-controls": ["analysis controls", "analysis resource selection", "GPU frame analysis", "live INI rendering", "native hunting", "native tracker", "Vulkan validation", "text check"],
    "analysis-formats": ["analysis export formats", "analysis resource selection", "GPU frame analysis", "GPU image shapes", "GPU target analysis", "native tracker", "Vulkan validation", "text check"],
    "analysis-selection": ["analysis resource selection", "GPU frame analysis", "GPU geometry analysis", "GPU indirect analysis", "GPU image shapes", "GPU target analysis", "native tracker", "Vulkan validation", "text check"],
    "gpu-analysis": ["GPU image shapes", "GPU frame analysis", "GPU geometry analysis", "GPU indirect analysis", "GPU target analysis", "native tracker", "Vulkan validation", "text check"],
    "hunting-overlay": ["persistent hunting overlay", "native hunting", "analysis controls", "live INI rendering", "owned Present on a swap chain", "native tracker", "Vulkan validation", "text check"],
    "native-storage-images": ["native storage images", "native application images", "native application shapes", "native descriptor bindings", "GPU frame analysis", "native tracker", "Vulkan validation", "text check"],
    "application-image-shapes": ["native application shapes", "native application images", "native image shapes", "native descriptor bindings", "GPU frame analysis", "native tracker", "Vulkan validation", "text check"],
    "native-image-shapes": ["native image shapes", "native image bindings", "native file bindings", "native descriptor bindings", "native application images", "native tracker", "Vulkan validation", "text check"],
    "native-images": ["native image bindings", "native file bindings", "native descriptor bindings", "native tracker", "Vulkan validation", "text check"],
    "native-application-images": ["native application images", "native image bindings", "native descriptor bindings", "native tracker", "Vulkan validation", "text check"],
    "module-publication": ["native game shader", "native compute", "persistent hunting overlay", "native hunting", "native file bindings", "native application images", "native tracker", "Vulkan validation", "text check"],
    "module-paths": ["native application images", "native file bindings", "native tracker", "text check"],
    "dynamic-geometry": ["GPU dynamic vertex input", "GPU dynamic geometry", "GPU geometry analysis", "GPU indirect analysis", "analysis resource selection", "native tracker", "pipeline recipes and variants", "Vulkan validation", "text check"],
    "native-mutable-images": ["mutable native file images", "native storage images", "native image shapes", "mutable native file buffers", "native image bindings", "native file bindings", "native tracker", "Vulkan validation", "text check"],
    "native-uniforms": ["native graphics uniforms", "native descriptor bindings", "native application images", "native file bindings", "native tracker", "Vulkan validation", "text check"],
    "native-mutable-files": ["mutable native file buffers", "native file bindings", "native descriptor bindings", "native resource slots", "native tracker", "Vulkan validation", "text check"],
    "native-files": ["native file bindings", "native descriptor bindings", "native resource slots", "native tracker", "Vulkan validation", "text check"],
    # A minute or so, for every change: text, the Python units, the expression cases, what each command
    # class does in a replaced draw, and the original as the judge.
    "quick": ["text check", "unit tests", "source-defined INI evaluation", "INI execution matrix", "as the judge"],
}


def selected(names: list[str], words: list[str]) -> list[str]:
    patterns = []
    for word in words:
        patterns += GROUPS.get(word.lower(), [word])
    return [n for n in names if any(p.lower() in n.lower() for p in patterns)]


def main() -> int:
    steps = list(STEPS)
    args = sys.argv[1:]
    if "--list" in args:
        for name, _ in STEPS:
            print(name)
        print("groups: " + ", ".join(sorted(GROUPS)))
        return 0
    if "--only" in args:
        at = args.index("--only")
        words = [w.strip() for w in (args[at + 1] if at + 1 < len(args) else "").split(",") if w.strip()]
        names = selected([n for n, _ in STEPS], words)
        if not names:
            print("run_all: --only matched no step")
            return 1
        steps = [(n, a) for n, a in STEPS if n in names]
    if "--no-build" not in args:
        build = [os.path.join(ROOT, "scripts", "build.py")]
        # A running process may hold the DLL in dist/. With EFMI_VK_DIST set, build and
        # test in that directory instead.
        if os.environ.get("EFMI_VK_DIST"):
            build += ["--out", os.environ["EFMI_VK_DIST"]]
        steps.insert(0, ("build", build))
    failed = []
    for name, args in steps:
        start = time.monotonic()
        proc = subprocess.run([sys.executable, "-B"] + args, cwd=HERE, capture_output=True, text=True, errors="replace")
        elapsed = time.monotonic() - start
        output = (proc.stdout or "") + (proc.stderr or "")
        print("[%s] %s (%.1fs)" % ("ok" if proc.returncode == 0 else "FAILED", name, elapsed))
        for line in output.splitlines():
            if proc.returncode != 0 or "checks passed" in line or line.startswith("built ") or "clean" in line:
                print("    " + line)
        if proc.returncode != 0:
            failed.append(name)
            if name == "build":
                break
    print("run_all: %s" % ("all steps passed" if not failed else "FAILED: " + ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
