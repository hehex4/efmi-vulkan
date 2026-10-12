#pragma once
#include "resource_cache.h"

// Keep source compatibility for the original texture-only callers.
namespace efmi::assets {
using TextureState=ResourceState;
using TextureSnapshot=ResourceSnapshot;
using TextureRequest=ResourceRequest;
using TextureHandle=ResourceHandle;
using TextureDevice=ResourceDevice;
using TextureBudgets=ResourceBudgets;
using TextureCacheStats=ResourceCacheStats;
using TextureCache=ResourceCache;
}
