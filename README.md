# DZC3DBrowser

Windows-only Qt 5.12 + Vulkan 1.0 / OpenGL 4.2 mesh viewer. Single Visual Studio solution, C++17, MSVC v143.

## Layout

```
code/                 app + vcxproj; VS debugger working directory
  res/                GLSL sources only (UTF-8 without BOM)
include/              reusable engine
windows/              exe OutDir
  Debug/              Debug exe
  Release/            Release exe
  shaders/            runtime GLSL copies + SPIR-V (shared by Debug and Release)
```

## Runtime shader directory

Visual Studio debugger working directory is `code/`. Loaders use this CWD-relative path:

```text
../windows/shaders/
```

Do not put machine-local absolute paths in runtime loaders.

| Backend | Files loaded at runtime |
|---------|-------------------------|
| Vulkan 2D | `2d_vert.spv`, `2d_frag.spv` |
| Vulkan 3D | `3d_vert.spv`, `3d_frag.spv` |
| Vulkan shadow | `3d_shadow_vert.spv`, `3d_shadow_frag.spv` |
| OpenGL 3D | `3d.vert`, `3d.frag`, `3d_shadow.vert`, `3d_shadow.frag` |

Missing files, invalid SPIR-V, or shader-module creation failures surface as a dialog with the path. Confirm the working directory is `code/` and that `windows/shaders/` contains the files above.

Launching with working directory `windows/` is **not** supported with the current relative path. Use Visual Studio (CWD = `code/`) or start the process with CWD = `code/`.

## Build

Open `vulkan-reference.sln`. Configs: **Debug|x64** and **Release|x64** only.

```
msbuild vulkan-reference.sln /p:Configuration=Debug /p:Platform=x64
```

Required on a new machine:

1. Visual Studio 2022 with MSVC v143.
2. Qt 5.12 (paths in the vcxproj / props are machine-local; do not “normalize” them unless asked).
3. Vulkan SDK. Set `VULKAN_SDK` so `$(VULKAN_SDK)\Bin\glslc.exe` exists. This repo’s SDK is currently `D:\vualkan-sdk`.

The `PrepareRuntimeShaders` target runs before compile: it copies GLSL from `code/res/` into `windows/shaders/` and compiles SPIR-V with `glslc --target-env=vulkan1.0`. After editing GLSL, rebuild so `windows/shaders/` refreshes.

`code/res/` keeps GLSL sources only. Debug and Release share `windows/shaders/`.
