#include "Render.h"

Render::Render() = default;
Render::~Render() = default;

bool Render::IsInitialized() const {
    return m_initialized;
}

bool Render::IsShuttingDown() const {
    return m_shuttingDown;
}

void Render::SetClearColor(float r, float g, float b, float a) {
    m_clearColor = { r, g, b, a };
}

void Render::SetFramebufferSize(uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;
}

void Render::SetFramebufferResized(bool resized) {
    m_framebufferResized = resized;
}

void Render::OnMouseDown(float nx, float ny, int button) {
    (void)nx;
    (void)ny;
    (void)button;
}

void Render::OnMouseMove(float nx, float ny) {
    (void)nx;
    (void)ny;
}

void Render::OnMouseUp(int button) {
    (void)button;
}

void Render::OnMouseWheel(float delta) {
    (void)delta;
}