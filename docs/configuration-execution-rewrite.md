# Configuration execution rewrite

User direction, 2026-11-16: the DX11 compatibility route must follow the original
configuration and resource execution model. Remove character/Mod package loading
completely, then continue Vulkan-native functionality. This supersedes the package
discovery, activation and isolation decisions in D-078/D-131/D-174/D-177.

## Original behavior

`DirectX11/IniHandler.cpp::ParseIncludedIniFiles` parses all included files into one
registry. Namespace reuse is not a reason to remove a file: the original handles
duplicate sections and retains distinct sections. `HackerContext.cpp::BeforeDraw`
executes matching shader command lists, which reach the original resource matcher.
`CommandList.cpp::CustomResource::Substantiate` creates a declared resource on its
first actual use. Neither file names nor skeleton initializer discovery select a
loading unit. The original Core's lists and resources are configuration content.

DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking
and one-context order. A Vulkan implementation must retain resource and command
owners, introduce proven dependencies and restore caller state explicitly. Merely
combining or renaming character packages does not implement this contract.

## Required implementation

1. Load the root INI and its original includes into one interpreter generation.
   Run Constants, Present, keys and matching lists through that generation. Do not
   scan for character INIs, pre-execute skeleton initializers, filter by namespace
   ownership or rebuild configurations after excluding a character package.
2. Use declaration identities for lazy resource creation and references. Mesh
   streams and skeleton buffers are resources; they are not a package activation
   unit. Execute the installed Core's commands through the ordinary GPU adapter.
3. Translate evaluated draw/dispatch/resource operations through shared Vulkan
   execution. Preserve the original matching and command order, including lists
   contributed by different included files. Supply verified application bindings.
4. Remove per-character controllers, discovery, package load/exchange/status paths
   and obsolete configuration options. Keep historical evidence clearly historical;
   update executable checks and release acceptance to configuration behavior.
5. Finish the native route's resource/queue scheduler, access phases, compute
   readiness and remaining capture boundaries. Real-game acceptance remains separate
   and requires the user's on-the-spot authorization.

## Evidence and status

D-303: the root-configuration fixture passes 10 checks for root includes,
cross-directory references, namespace sharing, inactive initializers, unused missing
resources and independent generation lifetime. The ordinary GPU adapter passes 40
headless checks for lazy instantiation, reference/copy semantics and complete resource
retirement, with zero core/synchronization validation diagnostics. The original runtime
regression passes 553 checks. Both new entry boundaries failed before their changes.

A read-only load of the installed root configuration also succeeds through this GPU
adapter: 2,172 variables, 848 referenced file resources, 44 original diagnostics and
zero adapter gaps during loading. No character/Core extension is supplied. This proves
configuration loading, not live application binding import or Core rendering.

The production entry is now being replaced in D-304. It loads `configuration`
(`EFMI_VK_CONFIGURATION`) as the root containing d3dx.ini. `auto` searches the
injected DLL's ancestors for that file. The original Include rules select inputs;
the backend neither enumerates character INIs nor accepts a package path.

Removed source paths: CharacterProgram discovery/catalogues, CharacterController,
CharacterGeneration, the package hash index/activation worker, native CharacterCore
substitution, per-Mod persistence migration and offline package-planning commands.
F10 exchanges the complete configuration. Global matching evaluates every matching
section, including distinct sections sharing a namespace across directories. The
public module ABI retains its historical names; no character loading is behind them.

Production checks pass 13 loading/matching/F10/module-exchange assertions, 60 native
GPU call assertions and 42 native file-resource assertions. Final affected-area checks
pass root 10 / owned GPU 40 / production 13; the original interpreter passes 553,
tracker 813 and retained offline format decoder 148. Source and four matching
development DLLs are 3.2.2 / ABI 964248fc551584a0. The separately verified candidate
remains 3.2.1. No game acceptance is claimed.

The removal deliberately does not retain the former package renderer as a fallback.
The ordinary GPU frontend and native resource executor are present, but generic mesh
bindings, application resource import for original Core execution, exact shader
semantics integration and ordered in-draw GPU work remain required. Existing capture
replay and package-rendering acceptance fixtures need migration; their historical
results do not validate this replacement. Finish these adapters before claiming
compatibility completion or returning to the remaining native queue work.

D-305 supplies generic immutable file VB/IB assignments and persistent IA state.
Declarations are uploaded on use through the shared resource cache. Every recorded
use retains its complete allocation/publication chain; application rebinds clear only
named slots. Actual pixels, 16/32-bit indices, bounds, post restoration, F10, two queues
and module exchange pass 52 checks, with 825 tracker and 553 original runtime checks.
Native call/file regressions pass 60/42. Layout and stride variants, retained application
IA inputs and owned GPU producer ordering remain incomplete. Source/development DLLs
are 3.2.3; this is sandbox evidence only.

D-306 connects the exact shader input index to the generic executor and resolves
supported original semantic edits and file strides without packages. Explicit, caller
and implicit calls retain independent asynchronous pipeline variants; selected shaders
are preserved, and temporary pipelines restore after each call. Classic, RenderPass2,
dynamic rendering and private target scopes pass actual-pixel checks (122 geometry
assertions). Fresh interpreter checks pass 553; tracker 825 and current pipeline tests
also pass after updating the former dynamic-layout refusal assertion. Source/development
DLLs are 3.2.4. Application IA facts, original Core resource imports and ordered in-draw
GPU commands remain active work; no real-game acceptance is claimed.

D-307 supplies actual application IA properties and retained references. Original size,
stride and offset rules and index-region matching pass 34 focused checks. Restoring a
reference requires the exact current original binding; foreign/recycled resources still
need ordered import. Tracker 838, migrated native aliases 89, original interpreter 553
and ABI 9 pass, alongside geometry/call/file checks. The empty-config file test's early
normal exit before probe connection is recorded; its isolated rerun passes. Source/four
development DLLs are 3.2.5. These results still do not establish live Core execution.

D-308 supplies the retained prepared buffer-command primitive needed for in-place
execution. Headless producer/consumer, ordinary execution and lifetime checks pass 59;
root/owned/registry/runtime checks also pass. Source/development DLLs are 3.2.6. It is
not wired into production draw interception yet: access footprints, original registry
imports/rollback, ordered packets, pass splitting and compute-state restoration remain.
See prepared-resource-commands.md for the contract and verification scope.

D-309 adds ordered physical buffer access metadata and bounded admission, passing 65
headless checks with clean validation. Source/development DLLs are 3.2.7. Original
registry rollback/imports and host insertion remain open; this does not enable live
Core execution or close the native queue scheduler.
