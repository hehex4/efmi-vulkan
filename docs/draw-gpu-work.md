# GPU work inside replaced draws (P2)

Candidate 1.6.3, 2026-10-20 (D-087, D-088). Sandbox only; not loaded into the game.

Some Mods run GPU work in the lists of a matched draw: they copy their own buffers, run a
CustomShader on them and bind the result to the draw. Typhoeus Luxuria deforms its chest this
way (`Typhoeus_Interaction.ini`): in the body's and the clothes' draw lists it copies the Mod's
position buffer into an RWBuffer, runs a deformation compute shader on it with the state of its
chest simulation and binds the result as `vb0`. Before P2 such a draw refused, and the game drew
its own body and clothes for those two components.

## What runs where

A replaced draw's plan runs with a scope of its own on the generation's owned GPU runtime
(`GpuResourceRuntime::ExecutionSite::Draw`, opened by the backend around the plan,
`CharacterGeneration::plan_draw`). In that scope:

- The draw's own bindings and draws keep the plan path: they become the replaced draw, recorded
  in the application's render pass with the game's pipelines.
- CustomShaders, copies into the Mod's resources (`ResourceX = ResourceY`, which the original
  copies by default, or `= copy ...`), references to a resource whose content owned work wrote,
  and everything inside a CustomShader run as owned work. They are recorded without a queue.
- Owned work never reaches the application's bindings: a command in it that reads or binds
  anything but the Mod's resources, IniParams, null or the compute stage's own slots refuses
  the plan (`vs-t0`, `vs-cb0`, `o0`, `vb0` as a source and the like). `store` refuses too: it
  needs GPU feedback in the middle of the application's recording.
- A vertex binding of a resource owned work wrote binds the owned buffer itself
  (`PlanItem::owned`, `CharacterDraw::owned`); the recording keeps it alive until the command
  buffer completes. Pixel and index bindings of such content refuse.
- Queries on the application's bindings are answered on the CPU by the host (the draw's render
  targets, the instance entry); those on the Mod's resources by the owned runtime.

## Order on the GPU

The recorded work is submitted on the application's graphics queue before its next submission
there (`OwnedPresenter::submit_pending`, from the submit hook), so it runs before the command
buffer whose draws read its results. Every owned operation begins with a barrier from all earlier
commands of the queue and every owned submission ends with one to all later commands, which
orders it against the frames before and after on the same queue. Work for draws in one
submission therefore runs before all of them, in draw order: a resource that owned work writes
several times for different draws of one submission holds its last content for all of them.
Typhoeus writes the same deformation for every pass of a frame, so this is what it draws.

The queue is fixed by the generation's first owned submission. Work recorded while another
queue submits stays pending until the right one submits or presents.

## Render targets in a plan

`@oN`, `@oD` and `oN === null` read the draw's render targets as its pipeline's subpass names
them: a color attachment at location N is what the fragment shader's output N writes, DX11 `oN`.
The identity distinguishes the attachments of one render pass; a bound game target matches no
TextureOverride of a Mod (a DX11 render target hash has no Vulkan counterpart). Other questions
about the draw's render targets refuse.

## The Typhoeus case

Typhoeus' classification of the camera pass reads `@o0`, `@o1` and `@o2`, then the Core's
`merged_skeleton_connected`. The native Core leaves that flag unset (the renderer's merged
skeleton is not the Core's), so the classification never finds a camera pass, the physics step
and the detection (which read the game's `vs-t0` and camera constant buffers) never run, and the
deformation runs from the state at rest. The body and clothes are the Mod's; the chest does not
move. Running the physics needs the game's bindings in Vulkan terms (the shader catalogue, T-030).

Replay of vk06, 360 frames: replaced draws 14130 (10300 before), no refused draw, owned vertex
streams bound 19354 times, owned draw work submitted 359 times, validation clean.

## A driver fault (D-088)

The deformation shader ships its DXBC cache, which runs through the exact translation with
`RoundingModeRTE` for 32-bit floats. NVIDIA's driver 591.86 (RTX 5060 Laptop GPU) ends the process
inside its shader compiler when a module with that mode has an `OpFDiv`, `OpFRem` or `OpFMod`;
a three-instruction compute shader shows it. On NVIDIA devices such a module loses the mode
(`frontend/gpu/driver_workarounds.h`); NVIDIA's float arithmetic rounds to nearest even without it.
Any translated shader with a division was affected, not only this Mod's.
