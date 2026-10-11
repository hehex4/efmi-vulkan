# EFMI Vulkan - Architecture

The target architecture of the product (interception, shadow, match, profile, engine, emit, prep, diag) is described in `docs/plan.md`, section 4. This file describes what exists in the repository.

## Data path of the development probe

`agent -> Python MCP stdio -> named pipe -> efmi_vk_layer.dll (inside the Vulkan process) -> tracked state / mapped memory -> MCP result`

The layer sits between the application and the driver in the Vulkan loader's layer chain. It forwards every call unchanged; the probe only observes.

## Components

Nothing is implemented yet. Sections for the layer, the sandbox, the server and the build and verification entry points are added as each lands.
