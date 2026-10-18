#include "png.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace efmi::assets {
namespace {
using Microsoft::WRL::ComPtr;
void require(HRESULT hr, const char* operation)
{
    if (SUCCEEDED(hr)) return;
    std::ostringstream message; message << operation << ": HRESULT 0x" << std::hex << uint32_t(hr);
    throw std::runtime_error(message.str());
}
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (result != RPC_E_CHANGED_MODE) require(result, "Initialize image decoder apartment"); }
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Reservation {
    std::shared_ptr<AllocationBudget> budget;
    uint64_t bytes;
    Reservation(std::shared_ptr<AllocationBudget> b, uint64_t n) : budget(std::move(b)), bytes(n)
    { if (budget && !budget->acquire(bytes)) throw std::runtime_error("Decoded image exceeds aggregate source budget"); }
    ~Reservation() { if (budget) budget->release(bytes); }
};
struct Format { GUID target; VkFormat vk; };
Format target_format(const GUID& source)
{
    // Preserve the pinned DirectXTK loader's channel semantics and precision.
    // Palette/RGB images expand to RGBA; grayscale stays a single channel.
    if (source == GUID_WICPixelFormat8bppGray || source == GUID_WICPixelFormatBlackWhite ||
        source == GUID_WICPixelFormat2bppGray || source == GUID_WICPixelFormat4bppGray)
        return {GUID_WICPixelFormat8bppGray, VK_FORMAT_R8_UNORM};
    if (source == GUID_WICPixelFormat16bppGray)
        return {GUID_WICPixelFormat16bppGray, VK_FORMAT_R16_UNORM};
    if (source == GUID_WICPixelFormat64bppRGBA || source == GUID_WICPixelFormat48bppRGB ||
        source == GUID_WICPixelFormat48bppBGR || source == GUID_WICPixelFormat64bppBGRA)
        return {GUID_WICPixelFormat64bppRGBA, VK_FORMAT_R16G16B16A16_UNORM};
    if (source == GUID_WICPixelFormat32bppBGRA)
        return {GUID_WICPixelFormat32bppBGRA, VK_FORMAT_B8G8R8A8_UNORM};
    if (source == GUID_WICPixelFormat32bppRGBA || source == GUID_WICPixelFormat24bppRGB ||
        source == GUID_WICPixelFormat24bppBGR || source == GUID_WICPixelFormat32bppBGR ||
        source == GUID_WICPixelFormat1bppIndexed || source == GUID_WICPixelFormat2bppIndexed ||
        source == GUID_WICPixelFormat4bppIndexed || source == GUID_WICPixelFormat8bppIndexed)
        return {GUID_WICPixelFormat32bppRGBA, VK_FORMAT_R8G8B8A8_UNORM};
    throw std::runtime_error("PNG pixel format needs another typed image adapter");
}
}
uint32_t png_pixel_bytes(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8_UNORM: return 1;
    case VK_FORMAT_R16_UNORM: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: return 4;
    case VK_FORMAT_R16G16B16A16_UNORM: return 8;
    default: return 0;
    }
}
bool decode_png(const uint8_t* data, size_t size, PngPixels& output, std::string& reason,
                std::shared_ptr<AllocationBudget> budget, uint32_t flags)
{
    reason.clear();
    try {
        if (flags & ~(png_force_srgb | png_ignore_srgb)) throw std::runtime_error("PNG loader flags need another adapter");
        static const uint8_t magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (!data || size < 33 || size > (128ull << 20) || std::memcmp(data, magic, sizeof(magic)))
            throw std::runtime_error("PNG signature or encoded size is invalid");
        Apartment apartment;
        ComPtr<IWICImagingFactory> factory;
        require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf())), "Create WIC factory");
        ComPtr<IWICStream> stream; require(factory->CreateStream(&stream), "Create image stream");
        require(stream->InitializeFromMemory(const_cast<BYTE*>(data), DWORD(size)), "Set image stream");
        ComPtr<IWICBitmapDecoder> decoder;
        require(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "Decode PNG");
        GUID container{}; require(decoder->GetContainerFormat(&container), "Read image container");
        if (container != GUID_ContainerFormatPng) throw std::runtime_error("Image container is not PNG");
        UINT frames = 0; require(decoder->GetFrameCount(&frames), "Read image frame count");
        if (frames != 1) throw std::runtime_error("Image frame count needs another adapter");
        ComPtr<IWICBitmapFrameDecode> frame; require(decoder->GetFrame(0, &frame), "Read PNG frame");
        UINT width = 0, height = 0; require(frame->GetSize(&width, &height), "Read PNG dimensions");
        if (!width || !height || width > 16384 || height > 16384) throw std::runtime_error("PNG dimensions exceed image budget");
        GUID original{}; require(frame->GetPixelFormat(&original), "Read PNG pixel format");
        auto format = target_format(original);
        // The loader's order: FORCE_SRGB, else unless IGNORE_SRGB the PNG sRGB chunk. Only the
        // 8-bit color formats have sRGB variants (LoaderHelpers::MakeSRGB).
        bool srgb = (flags & png_force_srgb) != 0;
        ComPtr<IWICMetadataQueryReader> metadata;
        if (!srgb && !(flags & png_ignore_srgb) && SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
            PROPVARIANT value{};
            srgb = SUCCEEDED(metadata->GetMetadataByName(L"/sRGB/RenderingIntent", &value)) && value.vt == VT_UI1;
            PropVariantClear(&value);
        }
        if (srgb && format.vk == VK_FORMAT_R8G8B8A8_UNORM) format.vk = VK_FORMAT_R8G8B8A8_SRGB;
        if (srgb && format.vk == VK_FORMAT_B8G8R8A8_UNORM) format.vk = VK_FORMAT_B8G8R8A8_SRGB;
        const uint64_t pitch = uint64_t(width) * png_pixel_bytes(format.vk), bytes = pitch * height;
        if (!bytes || bytes > (128ull << 20)) throw std::runtime_error("Decoded PNG exceeds image budget");
        PngPixels next;
        next.reservation = std::make_shared<Reservation>(std::move(budget), bytes);
        next.bytes.resize(size_t(bytes));
        if (original == format.target) {
            require(frame->CopyPixels(nullptr, UINT(pitch), UINT(bytes), next.bytes.data()), "Read PNG pixels");
        } else {
            ComPtr<IWICFormatConverter> converter;
            require(factory->CreateFormatConverter(&converter), "Create image format converter");
            BOOL can_convert = FALSE;
            require(converter->CanConvert(original, format.target, &can_convert), "Check image format conversion");
            if (!can_convert) throw std::runtime_error("PNG typed format conversion is unavailable");
            require(converter->Initialize(frame.Get(), format.target, WICBitmapDitherTypeErrorDiffusion,
                    nullptr, 0, WICBitmapPaletteTypeMedianCut), "Convert PNG pixel format");
            require(converter->CopyPixels(nullptr, UINT(pitch), UINT(bytes), next.bytes.data()), "Read converted PNG pixels");
        }
        next.metadata.format = format.vk; next.metadata.width = width; next.metadata.height = height;
        next.metadata.levels.push_back({width, height, 0, size_t(bytes)});
        output = std::move(next); return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
}
