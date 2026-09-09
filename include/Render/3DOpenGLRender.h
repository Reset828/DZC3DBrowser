#ifndef __3D_OPENGL_RENDER_H__
#define __3D_OPENGL_RENDER_H__

#include "OpenGLRender.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class OpenGLRender3D : public OpenGLRender {
public:
    OpenGLRender3D();
    ~OpenGLRender3D() override;

    OpenGLRender3D(const OpenGLRender3D&) = delete;
    OpenGLRender3D& operator=(const OpenGLRender3D&) = delete;

    void OnMouseDown(float nx, float ny, int button) override;
    void OnMouseMove(float nx, float ny) override;
    void OnMouseUp(int button) override;
    void OnMouseWheel(float delta) override;
    void SetWireframeEnabled(bool enabled);
    void SetGrayEnabled(bool enabled);
    void SetDyeEnabled(bool enabled);
    void SetLightAnalysisEnabled(bool enabled);
    void SetShadowTextureSize(uint32_t size);
    void SetLatitude(float latitude);
    void SetLightDate(int year, int month, int day);
    void SetLightTimeMinutes(int minutes);
    glm::vec3 GetSunDirection() const;
    bool IsSunAboveHorizon() const;
    void SetOrthographicEnabled(bool enabled);
    void SetOrbitCenter(const Vec3& normalizedCenter);
    void ResetView(float orbitDistance = 3.0f);
    void SetCoordinateNormalization(const Vec3& sourceCenter, float normalizationScale);
    bool IsWireframeEnabled() const override { return m_wireframeMode; }

    void RequestCoordReadback(float ndcX, float ndcY);
    bool HasNewWorldCoord() const;
    float GetLastWorldX() const;
    float GetLastWorldY() const;
    float GetLastWorldZ() const;

protected:
    bool OnInitialize() override;
    void OnShutdown() override;
    void OnBeginFrame() override;
    void OnEndFrame() override;
    void OnRecreateSwapchain() override;

    bool CreateRenderPass() override;
    bool CreatePipelines() override;
    bool CreateFramebuffers() override;

    bool CreateShaderProgram();
    void DestroyShaderProgram();
    bool CreateUniformBuffers();
    void DestroyUniformBuffers();
    void UpdateUniformBuffer();
    unsigned int CompileShader(unsigned int type, const char* source);
    unsigned int LinkProgram(unsigned int vert, unsigned int frag);
    void UpdateSunDirection();
    glm::vec3 ProjectToVirtualSphere(float nx, float ny) const;
    void ApplyConstrainedLocalRotation(const glm::vec3& localAxis, float angle);

    bool CreateDepthResources();
    void DestroyDepthResources();

    bool m_wireframeMode = false;
    bool m_grayEnabled = false;
    bool m_dyeEnabled = false;
    bool m_lightAnalysisEnabled = false;
    uint32_t m_shadowTextureSize = 2048;
    float m_latitude = 36.0f;
    int m_lightYear = 2000;
    int m_lightMonth = 3;
    int m_lightDay = 20;
    int m_lightTimeMinutes = 360;
    glm::vec3 m_sunDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    bool m_sunAboveHorizon = true;
    bool m_orthographicEnabled = false;

    glm::quat m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 m_orbitCenter = glm::vec3(0.0f);
    glm::vec3 m_panOffset = glm::vec3(0.0f);
    float m_orbitDistance = 3.0f;
    int m_mouseButton = -1;
    glm::vec2 m_lastMouse = glm::vec2(0.0f);
    glm::vec3 m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);

    bool CreateDepthReadbackResources();
    void DestroyDepthReadbackResources();
    void ProcessDepthReadback();

    bool m_depthReadbackRequested = false;
    float m_requestedNDCX = 0.0f;
    float m_requestedNDCY = 0.0f;
    float m_lastWorldCoord[3] = {};
    mutable bool m_newCoordAvailable = false;
    glm::mat4 m_normalizedToWorld = glm::mat4(1.0f);

    unsigned int m_program = 0;
    unsigned int m_ubo = 0;
};

#endif //__3D_OPENGL_RENDER_H__
