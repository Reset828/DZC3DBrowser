#ifndef __2D_OPENGL_RENDER_H__
#define __2D_OPENGL_RENDER_H__

#include "OpenGLRender.h"
#include <glm/glm.hpp>

class OpenGLRender2D : public OpenGLRender {
public:
    OpenGLRender2D();
    ~OpenGLRender2D() override;

    OpenGLRender2D(const OpenGLRender2D&) = delete;
    OpenGLRender2D& operator=(const OpenGLRender2D&) = delete;

    void OnMouseDown(float nx, float ny, int button) override;
    void OnMouseMove(float nx, float ny) override;
    void OnMouseUp(int button) override;
    void OnMouseWheel(float delta) override;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnBeginFrame() override;
    void OnEndFrame() override;
    bool CreatePipelines() override;

    bool CreateDescriptorSetLayout();
    bool CreateUniformBuffers();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();
    void DestroyUniformBuffers();
    void UpdateCameraUBO();

protected:
    glm::vec2 m_panOffset = glm::vec2(0.0f);
    float m_zoomLevel = 1.0f;

    int m_mouseButton = -1;
    glm::vec2 m_lastMouse = glm::vec2(0.0f);
};

#endif //__2D_OPENGL_RENDER_H__
