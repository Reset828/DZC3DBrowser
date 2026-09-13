#include "Render.h"
#include <utility>

// 绘制自身；Layer 则遍历子对象。
Render::Render() = default;
Render::~Render() = default;

// 设置 Mesh 创建工厂。
void Render::SetMeshFactory(MeshFactory factory) {
    m_meshFactory = std::move(factory);
}

// 通过工厂创建当前后端的 Mesh。
Object* Render::CreateMesh() {
    return m_meshFactory ? m_meshFactory(this) : nullptr;
}

// 查询渲染器是否已初始化。
bool Render::IsInitialized() const {
    return m_initialized;
}

// 查询渲染器是否正在关闭。
bool Render::IsShuttingDown() const {
    return m_shuttingDown;
}

// 设置清屏颜色。
void Render::SetClearColor(float r, float g, float b, float a) {
    m_clearColor = { r, g, b, a };
}

// 设置帧缓冲宽高。
void Render::SetFramebufferSize(uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;
}

// 标记帧缓冲需要重建。
void Render::SetFramebufferResized(bool resized) {
    m_framebufferResized = resized;
}

// 处理鼠标按下。
void Render::OnMouseDown(float nx, float ny, int button) {
    (void)nx;
    (void)ny;
    (void)button;
}

// 处理鼠标移动。
void Render::OnMouseMove(float nx, float ny) {
    (void)nx;
    (void)ny;
}

// 处理鼠标松开。
void Render::OnMouseUp(int button) {
    (void)button;
}

// 处理滚轮缩放。
void Render::OnMouseWheel(float delta) {
    (void)delta;
}
