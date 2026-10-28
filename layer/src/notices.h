// The overlay's notices on the presented image (D-137); the notices themselves are kept by util.cpp.
#pragma once

#include "common.h"

namespace efvk {

struct Device;
struct PresentWork;
class JsonWriter;

// On the presenting thread, after the presentation's other work: the visible notices drawn into the image
// of the first swap chain, the presentation then waiting for that. Never waits for the GPU (BUG-062):
// with every slot still in use the notices wait a presentation.
void notices_present(Device* dev, PresentWork* present);
// Before the swap chain's images go, and before the device goes (the application idled it).
void notices_swapchain_gone(Device* dev, const std::vector<VkImage>& images);
void notices_device_gone(Device* dev);
void notices_status(JsonWriter& w);

} // namespace efvk
