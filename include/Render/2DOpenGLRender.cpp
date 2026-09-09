#include "2DOpenGLRender.h"
#include <glm/gtc/matrix_transform.hpp>

OpenGLRender2D::OpenGLRender2D() {}

OpenGLRender2D::~OpenGLRender2D() {}

void OpenGLRender2D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

void OpenGLRender2D::OnMouseMove(float nx, float ny) {
    if (m_mouseButton < 0) return;
    glm::vec2 delta = glm::vec2(nx, ny) - m_lastMouse;
    m_lastMouse = glm::vec2(nx, ny);

    if (m_mouseButton == 0) {
        float aspect = (float)m_framebufferWidth / (float)m_framebufferHeight;
        float worldW = 2.0f * aspect * m_zoomLevel;
        float worldH = 2.0f * m_zoomLevel;
        m_panOffset.x -= delta.x * worldW;
        m_panOffset.y += delta.y * worldH;
    }
}

void OpenGLRender2D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

void OpenGLRender2D::OnMouseWheel(float delta) {
    m_zoomLevel *= (delta > 0.0f) ? 0.85f : 1.18f;
    m_zoomLevel = glm::clamp(m_zoomLevel, 0.01f, 100.0f);
}

bool OpenGLRender2D::OnInitialize() {
    return true;
}

void OpenGLRender2D::OnShutdown() {
    DestroyUniformBuffers();
}

void OpenGLRender2D::OnBeginFrame() {
    UpdateCameraUBO();
}

void OpenGLRender2D::OnEndFrame() {}

bool OpenGLRender2D::CreatePipelines() { return true; }
bool OpenGLRender2D::CreateDescriptorSetLayout() { return true; }
bool OpenGLRender2D::CreateUniformBuffers() { return true; }
bool OpenGLRender2D::CreateDescriptorPool() { return true; }
bool OpenGLRender2D::CreateDescriptorSets() { return true; }
void OpenGLRender2D::DestroyUniformBuffers() {}
void OpenGLRender2D::UpdateCameraUBO() {}
