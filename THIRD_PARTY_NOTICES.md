# Third-party components

EFMI Vulkan is released under the GNU General Public License version 3 (LICENSE). Its release
package contains, or was built from, the following components. Their licence texts are in the
package's `licenses` folder.

## Compiled into efmi_vk_inject.dll

| Component | Version | Licence | Source |
|---|---|---|---|
| 3DMigoto engine, as packaged by XXMI-Libs-Package (DirectX11, D3D_Shaders, HLSLDecompiler headers) | commit a7847eae82aaac32175c9a7d199673d5c9323bcd | GPL-3.0 | https://github.com/SpectrumQT/XXMI-Libs-Package |
| wwmi-dx12 (reshade_addon/migoto port) | commit 113f42cc2dc5702dbba624ee16abac96d68ed3b5 | GPL-3.0 | https://github.com/lekarsenten/wwmi-dx12 |
| DirectXTK DDSTextureLoader and WICTextureLoader (in XXMI-Libs-Package) | as above | MIT | https://github.com/microsoft/DirectXTK |
| crc32c-hw (in XXMI-Libs-Package) | 1.0.5 | zlib | https://github.com/robertvazan/crc32c-hw |
| PCRE2 (in XXMI-Libs-Package, static library) | 10.30 | BSD-3-Clause | https://github.com/PCRE2Project/pcre2 |
| SPIRV-Reflect | commit e55086b044225f9b511ae44dbf9b079d3625943f (1.4.321.0) | Apache-2.0 | https://github.com/KhronosGroup/SPIRV-Reflect |
| Vulkan-Headers, SPIRV-Headers (headers) | 1.4.321 | Apache-2.0, MIT | https://github.com/KhronosGroup |

## Shipped as separate libraries

| File | Component | Licence | Source |
|---|---|---|---|
| dxcompiler.dll | DirectX Shader Compiler 1.9.2609 (unmodified release) | University of Illinois/NCSA and Apache-2.0 with LLVM exceptions | https://github.com/microsoft/DirectXShaderCompiler |
| SPIRV-Tools-shared.dll | SPIRV-Tools, as built with Vulkan-ValidationLayers 1.4.321 | Apache-2.0 | https://github.com/KhronosGroup/SPIRV-Tools |
| efmi-vkd3d-shader.dll | vkd3d-shader of vkd3d 2.1 (unmodified release source) | LGPL-2.1-or-later | https://dl.winehq.org/vkd3d/source/vkd3d-2.1.tar.xz |

efmi-vkd3d-shader.dll is built from the unmodified vkd3d 2.1 source by `scripts/build_bytecode_compiler.py`
of this repository (its parser files generated with winflexbison 2.5.25, compiled with w64devkit
1.23.0's GCC). It is a separate library the layer loads at run time and may be replaced by another
build of the same source.

dxil.dll of the DirectX Shader Compiler is not part of the package: the layer compiles to SPIR-V and
does not sign DXIL.
