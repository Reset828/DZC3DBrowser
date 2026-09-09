#ifndef __OPENGL_RENDER_H__
#define __OPENGL_RENDER_H__

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

class QRunnable;
class QOpenGLContext;
class QOpenGLFunctions_4_2_Core;
class QWindow;

#ifndef __ENGINE_VEC_TYPES_H__
#define __ENGINE_VEC_TYPES_H__
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { float m[4][4]; };
struct Rect2D { int x, y, width, height; };
#endif

class OpenGLRender {
public:
    OpenGLRender();
    virtual ~OpenGLRender();

    OpenGLRender(const OpenGLRender&) = delete;
    OpenGLRender& operator=(const OpenGLRender&) = delete;

    enum DrawTopology {
        DT_TRIANGLE = 0,
        DT_TRIANGLE_WIREFRAME,
        DT_LINE,
        DT_POINT,
        DT_COUNT
    };

    virtual bool IsWireframeEnabled() const { return false; }

    bool Initialize(const char* appName, uint32_t width, uint32_t height);
    virtual void Shutdown();
    bool IsInitialized() const;

    void Quiesce();

    void SetClearColor(float r, float g, float b, float a);

    virtual bool BeginFrame();
    virtual void EndFrame();
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1);
    void SetPolygonWireframe(bool enabled);

    QOpenGLFunctions_4_2_Core* GetFunctions() const;
    unsigned int GetCurrentProgram() const;

    void WaitForIdle();
    void SubmitAsync(QRunnable* task);
    bool IsShuttingDown() const;

    void SetContext(QOpenGLContext* context);
    void SetWindow(QWindow* window);
    QOpenGLContext* GetContext() const;
    QWindow* GetWindow() const;

    void SetFramebufferSize(uint32_t width, uint32_t height);
    void SetFramebufferResized(bool resized);

    virtual void OnMouseDown(float nx, float ny, int button);
    virtual void OnMouseMove(float nx, float ny);
    virtual void OnMouseUp(int button);
    virtual void OnMouseWheel(float delta);

protected:
    virtual bool OnInitialize() { return true; }
    virtual void OnShutdown() {}

    virtual void OnBeginFrame() {}
    virtual void OnEndFrame() {}

    virtual void OnRecreateSwapchain() {}

    virtual bool CreateRenderPass();
    virtual bool CreatePipelines();
    virtual bool CreateFramebuffers();

    std::vector<char> ReadShaderFile(const std::string& filename);

protected:
    bool m_initialized = false;
    uint32_t m_framebufferWidth = 800;
    uint32_t m_framebufferHeight = 600;

    Vec4 m_clearColor = { 0.1f, 0.1f, 0.12f, 1.0f };

    QOpenGLContext* m_context = nullptr;
    QWindow* m_window = nullptr;
    QOpenGLFunctions_4_2_Core* m_functions = nullptr;
    unsigned int m_currentProgram = 0;

    bool m_framebufferResized = false;
    std::atomic<bool> m_shuttingDown{ false };
};

#endif //__OPENGL_RENDER_H__
