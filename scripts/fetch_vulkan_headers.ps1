# Fetches the Vulkan headers this project builds against.
#
# Source : https://github.com/KhronosGroup/Vulkan-Headers
# Version: tag vulkan-sdk-1.4.321.0 (matches the installed runtime vulkan-1.dll 1.4.321)
# Target : F:\vulkan\Vulkan-Headers (outside the repository, see .ai-memory/DECISIONS.md D-005)
#
# The script only clones; it runs nothing from the clone and changes no system setting.

param(
    [string]$Root = "F:\vulkan",
    [string]$Tag = "vulkan-sdk-1.4.321.0"
)

$ErrorActionPreference = "Stop"
$target = Join-Path $Root "Vulkan-Headers"
$header = Join-Path $target "include\vulkan\vulkan_core.h"

if (Test-Path $header) {
    $have = (& git -C $target describe --tags --always) 2>$null
    Write-Output "Vulkan headers already present at $target ($have)"
    exit 0
}

if (Test-Path $target) {
    throw "$target exists but has no include\vulkan\vulkan_core.h; remove it or pass -Root"
}

New-Item -ItemType Directory -Force $Root | Out-Null
& git clone --depth 1 --branch $Tag --config advice.detachedHead=false `
    https://github.com/KhronosGroup/Vulkan-Headers.git $target
if ($LASTEXITCODE -ne 0) { throw "git clone failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $header)) { throw "clone finished but $header is missing" }
$commit = & git -C $target rev-parse HEAD
Write-Output "Vulkan headers $Tag ($commit) are at $target"
