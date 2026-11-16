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

Backend migration and package-path removal remain active work. Production layer and
candidate are still 3.2.1; these prerequisite checks are not game acceptance.
