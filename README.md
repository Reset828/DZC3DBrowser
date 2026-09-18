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

Runtime assets are resolved **relative to the executable**, not the process working directory (`include/Path/AssetPath.*`). The exe lives in `windows/Debug` or `windows/Release`; the runtime shaders live in `windows/shaders/`. The loader walks up from the exe directory until it finds a `shaders` folder, so the same binary works no matter what the current working directory is (Visual Studio, Explorer double-click, shortcut, or any CWD).

Do not put machine-local absolute paths in runtime loaders.

| Backend | Files loaded at runtime |
|---------|-------------------------|
| Vulkan 2D | `2d_vert.spv`, `2d_frag.spv` |
| Vulkan 3D | `3d_vert.spv`, `3d_frag.spv` |
| Vulkan shadow | `3d_shadow_vert.spv`, `3d_shadow_frag.spv` |
| OpenGL 3D | `3d.vert`, `3d.frag`, `3d_shadow.vert`, `3d_shadow.frag` |

Missing files, invalid SPIR-V, or shader-module creation failures surface as a dialog with the resolved absolute path. Confirm that a `shaders/` directory with the files above sits next to (or above) the executable.

Launching the exe directly (e.g. double-clicking it in `windows/Debug` or `windows/Release`) is supported, because resolution is anchored to the exe location rather than the working directory.

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

## Backends

Vulkan is the primary backend and the startup default. OpenGL 4.2 Core is a switchable 3D backend with the same mesh, camera, display-mode, shadow, and world-coordinate features.

| Capability | Vulkan | OpenGL |
|------------|--------|--------|
| 3D mesh, orbit camera, ortho | supported | supported |
| Gray / dye / wireframe | supported | supported |
| Light analysis + shadow map | supported | supported |
| World-coord from depth | supported | supported |
| 2D | partial (see task 0.4) | unsupported |

The status bar shows the active backend. Switching backends clears the last world-coordinate readout. OpenGL 2D has no shader or draw path; do not treat the 2D/3D switch as equivalent on OpenGL.

## Importing models

`File → Open` accepts `*.obj`, `*.gltf`, `*.glb` (one `.obj` per file is still supported as a compatibility format).

- **OBJ**: one mesh; SubMeshes split by `usemtl`; material base color from MTL `Kd` (default white). `map_Kd` is read as a base-color texture reference (task 1.3).
- **glTF 2.0**: parsed with Qt's JSON reader plus manual binary reads — no third-party dependency. Supported subset:
  - `.gltf` (JSON) and `.glb` (binary container).
  - Buffers: embedded GLB BIN chunk, `data:` base64 URIs, or external files relative to the `.gltf`.
  - Node hierarchy with local transform (`matrix` or TRS). The world transform is baked into vertex positions at import; nodes are kept as name/hierarchy metadata.
  - Mesh primitives in `TRIANGLES` mode; `POSITION`, `NORMAL`, `TEXCOORD_0`, `COLOR_0`; indices as ubyte/ushort/uint.
  - Accessors: float or normalized integer `VEC2/3/4`, compact or `byteStride` interleaved. `sparse` accessors are reported unsupported.
  - `materials` (`pbrMetallicRoughness.baseColorFactor` / `baseColorTexture`), `textures`, `images`, `samplers` are parsed; image bytes (embedded `bufferView` / `data:` URI / external file) are resolved and fed to the texture system (task 1.3).
  - Not yet: animation, skinning, morph targets, `KHR_*` extensions, non-triangle primitive modes.
- Import warnings/errors, shadow-map status, renderer errors, and a one-line texture summary appear in the message panel below the viewport (one message per line, errors in red); there are no modal dialogs.

## Texture system (task 1.3)

A backend-independent texture resource system decodes referenced images and creates GPU textures. It does **not** sample them in a shader yet (that is task 1.4).

- **Decode**: images are decoded with Qt's `QImage` (PNG/JPEG/...), normalized to tightly packed RGBA8. No third-party image library.
- **CPU cache** (`code/TextureCache.*`, owned by `MainWindow`): keyed by normalized absolute path for external files, or by an FNV-1a content hash for embedded images. Survives model delete and backend switches, so the same image is decoded once.
- **GPU resources** (`include/Texture/`): `VKTexture` (VkImage + memory + ImageView + Sampler) and `GLTexture` (texture + sampler object). Created through the `Render` base interface (`CreateTexture` / `DestroyTexture` / `ProcessDeferredTextureDestruction` / `ReleaseAllTextures`), returning an opaque `TextureHandle`.
- **Upload**: staging buffer -> image copy; layout transitions to `TRANSFER_DST` then `SHADER_READ_ONLY` (Vulkan). OpenGL uses `glTexImage2D` + `glGenerateMipmap`.
- **Mipmaps**: full chain generated at runtime (`vkCmdBlitImage` per level on Vulkan; `glGenerateMipmap` on OpenGL).
- **Color vs data textures**: `TextureSemantic` (`Color`/`Emissive` -> sRGB `R8G8B8A8_SRGB`; `Normal`/`Roughness`/`Metallic`/`Occlusion`/`Generic` -> linear `R8G8B8A8_UNORM`). In 1.3, `baseColor` is treated as a `Color` (sRGB) texture.
- **Deferred destroy**: `DestroyTexture` only enqueues a handle; the renderer frees it after a few frames (`kTextureDestroyDelayFrames`) or immediately on `ReleaseAllTextures` (Shutdown/Quiesce). Triggered by model delete, backend switch, and close.
- **Dedup**: the renderer keys live GPU textures by `TextureDesc::cacheKey`, so identical images shared across materials/models upload once (reference-counted).

Two self-contained samples live in `samples/`: `cube.gltf` (base64-embedded buffer + image) and `cube.glb`. See `samples/README.md`.
