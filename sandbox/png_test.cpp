#include "../frontend/assets/png.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
using namespace efmi::assets;
int wmain(int argc, wchar_t** argv)
{
    try {
        if (argc < 3 || argc > 4) throw std::runtime_error("Expected input PNG, project pixel output and optional byte budget");
        std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > (128ull << 20)) throw std::runtime_error("Invalid PNG test input");
        std::vector<uint8_t> data(size_t(file.tellg())); file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), data.size());
        if (!file) throw std::runtime_error("Incomplete PNG test input");
        auto budget = std::make_shared<AllocationBudget>(argc == 4 ? std::stoull(argv[3]) : 128ull << 20);
        PngPixels pixels; pixels.metadata.width = 999; pixels.bytes = {1, 2, 3};
        std::string reason;
        if (!decode_png(data.data(), data.size(), pixels, reason, budget)) {
            if (pixels.metadata.width != 999 || pixels.bytes != std::vector<uint8_t>{1, 2, 3} || budget->used())
                throw std::runtime_error("Failed decode changed output or leaked its reservation");
            fprintf(stderr, "PNG refused without changing output: %s\n", reason.c_str()); return 4;
        }
        if (budget->used() != pixels.bytes.size()) throw std::runtime_error("Decoded pixel reservation is unbalanced");
        std::ofstream out(std::filesystem::path(argv[2]), std::ios::binary);
        out.write(reinterpret_cast<const char*>(pixels.bytes.data()), pixels.bytes.size());
        if (!out) throw std::runtime_error("Cannot save PNG reference pixels");
        printf("{\"width\":%u,\"height\":%u,\"format\":%u,\"bytes\":%zu,\"source_peak\":%llu}\n",
            pixels.metadata.width, pixels.metadata.height, unsigned(pixels.metadata.format), pixels.bytes.size(),
            static_cast<unsigned long long>(budget->peak()));
        pixels = {};
        if (budget->used()) throw std::runtime_error("Retired decoded pixels retain a reservation");
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "PNG test error: %s\n", e.what()); return 1; }
}
