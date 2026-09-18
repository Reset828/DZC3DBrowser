#ifndef __RENDER_H__
#define __RENDER_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "Math/EngineTypes.h"
#include "Texture/TextureTypes.h"


class Object;

/******************************************************
    Render                 生命周期 / 帧循环 / Mesh 工厂
      |
      +-- GLRender         OpenGL 上下文与绘制
      |     +-- GLRender2D
      |     +-- GLRender3D
      +-- VKRender         Vulkan 设备 / 交换链 / 管线
            +-- VKRender2D
            +-- VKRender3D
********************************************************/
class Render {
public:
    virtual ~Render();

    Render(const Render&) = delete;
    Render& operator=(const Render&) = delete;

    enum DrawTopology {
        DT_TRIANGLE = 0,
        DT_TRIANGLE_WIREFRAME,
        DT_LINE,
        DT_POINT,
        DT_COUNT
    };

    // 初始化渲染器及其后端资源。
    virtual bool Initialize(const char* appName, uint32_t width, uint32_t height) = 0;
    // 关闭渲染器并释放资源。
    virtual void Shutdown() = 0;
    // 等待异步任务完成并使渲染器静止。
    virtual void Quiesce() = 0;

    // 开始一帧渲染。
    virtual bool BeginFrame() = 0;
    // 结束当前帧并提交结果。
    virtual void EndFrame() = 0;
    // 提交索引绘制命令。
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) = 0;

    // 等待 GPU 与异步任务完成。
    virtual void WaitForIdle() = 0;
    using AsyncTask = std::function<void()>;
    // 把任务丢进线程池异步执行。
    virtual void SubmitAsync(AsyncTask task) = 0;

    using MeshFactory = std::function<Object*(Render*)>;
    // 设置当前后端的 Mesh 工厂。
    void SetMeshFactory(MeshFactory factory);
    // 通过工厂创建当前后端的 Mesh。
    Object* CreateMesh();

    // 创建一张 GPU 纹理（RGBA8 像素，语义决定 sRGB/线性与 mipmap）。返回句柄；0 表示失败。
    virtual TextureHandle CreateTexture(const TextureDesc& desc) { (void)desc; return 0; }
    // 标记释放一张纹理；实际销毁延迟到安全时机（帧计数队列）。
    virtual void DestroyTexture(TextureHandle handle) { (void)handle; }
    // 推进延迟销毁队列（每帧调用一次）。
    virtual void ProcessDeferredTextureDestruction() {}
    // 立即销毁全部纹理（Shutdown/Quiesce 用）。
    virtual void ReleaseAllTextures() {}
    // 查询某句柄是否仍然有效。
    virtual bool IsTextureValid(TextureHandle handle) const { (void)handle; return false; }

    // 查询渲染器是否已初始化。
    bool IsInitialized() const;
    // 查询渲染器是否正在关闭。
    bool IsShuttingDown() const;
    // 返回最近一次初始化或着色器加载失败的说明。
    const std::string& GetLastError() const;
    // 查询逻辑设备是否已丢失。
    bool IsDeviceLost() const;
    // 记录失败原因（供窗口层在 Instance/Surface 创建失败时使用）。
    void ReportError(std::string message);

    // 设置清屏颜色。
    void SetClearColor(float r, float g, float b, float a);
    // 设置帧缓冲宽高。
    void SetFramebufferSize(uint32_t width, uint32_t height);
    // 标记帧缓冲需要重建。
    void SetFramebufferResized(bool resized);

    // 查询是否开启线框。
    virtual bool IsWireframeEnabled() const { return false; }
    // 处理鼠标按下。
    virtual void OnMouseDown(float nx, float ny, int button);
    // 处理鼠标移动。
    virtual void OnMouseMove(float nx, float ny);
    // 处理鼠标松开。
    virtual void OnMouseUp(int button);
    // 处理滚轮缩放。
    virtual void OnMouseWheel(float delta);

protected:
    Render();

    // 记录最近一次失败原因。
    void SetLastError(std::string message);
    // 标记设备丢失并记录原因。
    void MarkDeviceLost(std::string message);

    bool m_initialized = false;
    uint32_t m_framebufferWidth = 800;
    uint32_t m_framebufferHeight = 600;
    Vec4 m_clearColor = { 0.1f, 0.1f, 0.12f, 1.0f };
    bool m_framebufferResized = false;
    std::atomic<bool> m_shuttingDown{ false };
    bool m_deviceLost = false;
    MeshFactory m_meshFactory;
    std::string m_lastError;
};

#endif //__RENDER_H__
