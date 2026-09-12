#ifndef __RENDER_H__
#define __RENDER_H__

#include <atomic>
#include <cstdint>
#include <functional>

#include "Math/EngineTypes.h"


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

    virtual bool Initialize(const char* appName, uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() = 0;
    virtual void Quiesce() = 0;

    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) = 0;

    virtual void WaitForIdle() = 0;
    using AsyncTask = std::function<void()>;
    virtual void SubmitAsync(AsyncTask task) = 0;

    bool IsInitialized() const;
    bool IsShuttingDown() const;

    void SetClearColor(float r, float g, float b, float a);
    void SetFramebufferSize(uint32_t width, uint32_t height);
    void SetFramebufferResized(bool resized);

    virtual bool IsWireframeEnabled() const { return false; }
    virtual void OnMouseDown(float nx, float ny, int button);
    virtual void OnMouseMove(float nx, float ny);
    virtual void OnMouseUp(int button);
    virtual void OnMouseWheel(float delta);

protected:
    Render();

    bool m_initialized = false;
    uint32_t m_framebufferWidth = 800;
    uint32_t m_framebufferHeight = 600;
    Vec4 m_clearColor = { 0.1f, 0.1f, 0.12f, 1.0f };
    bool m_framebufferResized = false;
    std::atomic<bool> m_shuttingDown{ false };
};

#endif //__RENDER_H__