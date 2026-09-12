# Vulkan-Reference

Windows-only Qt 5.12 + Vulkan 1.0 mesh viewer. MSVC v143, C++17, single VS solution. No tests, no CMake, no CI.

## Session rules

- Read this file before every task.
- Modules (including new ones) must stay high-cohesion / low-coupling.
- New or edited source files (`.h` / `.cpp` and VS project files) must be **UTF-8 with BOM** and **CRLF**.
- Reusable classes and helpers live in `include/`. Same pattern as the existing `Object` / `Layer` / `Render` / `VertexType` types.
- Before adding or changing code, search `include/` first. Reuse existing APIs. Do not duplicate them.
- After a task: static checks only (read/diff/diagnostics). Do not compile, run, or add tests unless asked.

## Layout (trust the filesystem)

`git ls-files` still lists the **old flat layout** (sources at repo root, `render/`, `shaders/`). The working tree is split:

```
code/                 app + vcxproj
  MainWindow.*        Qt UI, 2D/3D switch, OBJ load, scene rebuild
  QWindowVulkan.*     owns VkInstance + Win32 VkSurfaceKHR
  VKMesh.*        VKObject that uploads/draws a mesh
  *Runnable.*         QRunnable: OBJ parse, staging-buffer upload
  res/                GLSL + SPIR-V (see Shaders)
include/              reusable engine; .h and .cpp live together, compiled into the same exe
  Object/             VKObject — scene node + GPU buffers
  Layer/              Layer — backend-independent child list
  Render/             Render, GLRender/2D/3D, VKRender/2D/3D
  VertexType/         Vertex2D / Vertex3D / UBO layouts
windows/              OutDir (exe). VS run CWD.
obj/                  sample OBJ
```

Include path is `$(ProjectDir)..\include`, so includes look like `"Render/VKRender.h"`.

## Boot / frame

`code/main.cpp` → `QApplication` → `MainWindow`.

1. `SetupVulkan`: wrap `QWindowVulkan` in `QWidget::createWindowContainer`.
2. `QWindowVulkan::exposeEvent` creates instance + `vkCreateWin32SurfaceKHR`, then `SetInstance` / `SetSurface` / `Initialize`.
3. `QTimer` ~16 ms: `BeginFrame` → `m_scene->Render` → `EndFrame`.

`SetInstance` sets `m_externalInstance`. `VKRender::Shutdown` must **not** destroy instance/surface; `QWindowVulkan` owns them.

2D/3D switch (`SwitchTo2D` / `SwitchTo3D`): stop timer, `scene->Clear`, `Shutdown` + `delete` renderer, construct `VKRender2D` or `VKRender3D`, reuse the same instance/surface, `Initialize`, `RebuildSceneMeshes`.

## Where to look

| Task | Location |
|------|----------|
| Reusable Vulkan / scene API | `include/` — search here first |
| Instance, device, swapchain, pipelines | `include/Render/VKRender.*` |
| 2D pan/zoom or 3D orbit/depth/readback | `include/Render/VKRender.*` (2D/3D classes) |
| Scene node, vertex/index buffers | `include/Object/VKObject.*` |
| Child list | `include/Layer/Layer.*` |
| Vertex bindings (keep in sync with GLSL) | `include/VertexType/VertexTypes.*` |
| Qt window / surface | `code/QWindowVulkan.*` |
| UI, file open, 2D/3D switch | `code/MainWindow.*` |
| Mesh upload / draw | `code/VKMesh.*` |
| OBJ parse | `code/ObjParseRunnable.*` |
| Staging upload on thread pool | `code/BufferUploadRunnable.*` + `VKRender::SubmitAsync` |

## include/ APIs (do not reimplement)

- `VKRender` — `Initialize` / `Shutdown` / `Quiesce`, `BeginFrame` / `EndFrame` / `DrawIndexed`, buffer helpers, `ReadShaderFile`, `SubmitAsync`, mouse hooks. Non-copyable.
- `VKRender2D` / `VKRender3D` — both are declared and implemented in `VKRender.*`. Override `OnInitialize` / `CreatePipelines` / camera UBO. 3D also: depth, wireframe/gray/dye/ortho, world-coord readback.
- `VKObject` — `SetRender` / parent / visible / color; `CreateVertexBuffer` / `CreateIndexBuffer` / `DestroyBuffers`. `Render()` is pure virtual.
- `Layer` — `AddChild` / `RemoveChild` / `Clear`; `Render` walks `Object*` children under `shared_mutex`.
- `Vertex2D` / `Vertex3D` — `GetBindingDescription` / `GetAttributeDescriptions`.

App-only (keep out of `include/`): anything with `Q_OBJECT`, `MainWindow`, `QWindowVulkan`, `VKMesh`, the two `QRunnable`s.

## Conventions

- `Q_OBJECT` types must be `<QtMoc>` in `code/vulkan-reference.vcxproj`, not `ClInclude`. Only `MainWindow` and `QWindowVulkan` are moc'd today.
- `include/` headers use `#ifndef __FOO_H__`. `code/` mixes `#pragma once`.
- Debug builds enable `VK_LAYER_KHRONOS_validation`. `VK_CHECK_RESULT` exists only in `VKRender.cpp`.
- Async work (`QThreadPool`, `SubmitAsync`): check `IsShuttingDown()`, load/upload generation, and `VKMesh::m_alive` before touching GPU or UI objects. `Quiesce` waits the pool, then `vkDeviceWaitIdle`.
- New reusable module: new folder under `include/`, `.h`+`.cpp` together, add both to the vcxproj. Do not leak Qt widgets or `Q_OBJECT` into `include/`.

## Anti-patterns

- Do not recreate buffers, shader loading, pipelines, or the scene graph in `code/` if `include/` already does it.
- Do not destroy `VkInstance` / `VkSurfaceKHR` from `VKRender` when `m_externalInstance` is set.
- Do not add GLFW call sites. `VulkanConfig.props` still links `glfw3.lib`; the app uses Qt + `VK_KHR_win32_surface` only.
- Do not write LF-only or BOM-less `.h`/`.cpp`. **GLSL is the opposite:** `.vert`/`.frag` must be UTF-8 **without** BOM (`glslc` rejects BOM; the old `shaders/compile.bat` strips it).

## Shaders

- Sources live at `code/res/{2d,3d}.{vert,frag}`. SPIR-V names: `{2d,3d}_{vert,frag}.spv`.
- Runtime load is CWD-relative: `"shaders/2d_vert.spv"` / `"shaders/3d_vert.spv"` (see `CreatePipelines`). VS `OutDir` is `windows/`, so SPIR-V must be at `windows/shaders/`.
- vcxproj lists `res\shaders\*.vert` but files currently sit in `code/res/` with no nested `shaders/` folder. Keep the vcxproj in sync if you move them.
- Compile with `D:\VulkanSDK\Bin\glslc.exe` (machine-local). After editing GLSL, rebuild SPIR-V and copy into `windows/shaders/`.

## Build

Open `vulkan-reference.sln`. Configs: **Debug|x64** and **Release|x64** only.

```
msbuild vulkan-reference.sln /p:Configuration=Debug /p:Platform=x64
```

Paths inside `code/vulkan-reference.vcxproj` and `code/VulkanConfig.props` are machine-local — do not "normalize" them unless asked:

- Qt headers: `D:\qt5.12.12\msvc2022_64\include`
- Debug Qt libs: `D:\qt_2\5.12.12\msvc2022_64\lib` (different tree than headers)
- `VulkanConfig.props`: `D:\vulkanSDK\Include` (+ leftover glfw)
- Debug extra lib dir: `D:\vualkan-sdk\Lib` (typo'd folder name)

Windows subsystem, `mainCRTStartup`, `/utf-8`, Unicode charset.
