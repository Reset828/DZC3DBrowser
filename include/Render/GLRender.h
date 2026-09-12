#ifndef __GL_RENDER_H__
#define __GL_RENDER_H__

#include <cstdint>
#include <string>
#include <vector>

#include "Render.h"

class QRunnable;
class QOpenGLContext;
class QOpenGLFunctions_4_2_Core;
class QWindow;


class GLRender : public Render {
public:
    GLRender();
    ~GLRender() override;

    GLRender(const GLRender&) = delete;
    GLRender& operator=(const GLRender&) = delete;

    bool Initialize(const char* appName, uint32_t width, uint32_t height) override;
    void Shutdown() override;
    void Quiesce() override;
    bool BeginFrame() override;
    void EndFrame() override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) override;
    void SetPolygonWireframe(bool enabled);

    QOpenGLFunctions_4_2_Core* GetFunctions() const;
    unsigned int GetCurrentProgram() const;
    virtual unsigned int GetShadowProgram() const { return 0; }

    void WaitForIdle() override;
    void SubmitAsync(Render::AsyncTask task) override;
    void SubmitAsync(QRunnable* task);
    void SetContext(QOpenGLContext* context);
    void SetWindow(QWindow* window);
    QOpenGLContext* GetContext() const;
    QWindow* GetWindow() const;
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

    QOpenGLContext* m_context = nullptr;
    QWindow* m_window = nullptr;
    QOpenGLFunctions_4_2_Core* m_functions = nullptr;
    unsigned int m_currentProgram = 0;
};



#include <glm/glm.hpp>

class GLRender2D : public GLRender {
public:
    GLRender2D();
    ~GLRender2D() override;

    GLRender2D(const GLRender2D&) = delete;
    GLRender2D& operator=(const GLRender2D&) = delete;

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

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

class GLRender3D : public GLRender {
public:
    GLRender3D();
    ~GLRender3D() override;

    GLRender3D(const GLRender3D&) = delete;
    GLRender3D& operator=(const GLRender3D&) = delete;

    void OnMouseDown(float nx, float ny, int button) override;
    void OnMouseMove(float nx, float ny) override;
    void OnMouseUp(int button) override;
    void OnMouseWheel(float delta) override;
    void SetWireframeEnabled(bool enabled);
    void SetGrayEnabled(bool enabled);
    void SetDyeEnabled(bool enabled);
    void SetLightAnalysisEnabled(bool enabled);
    void SetShadowTextureSize(uint32_t size);
    uint32_t GetShadowTextureSize() const;
    bool IsShadowMapReady() const;
    std::string TakeShadowMapStatus();
    void SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid);
    bool BeginShadowPass();
    void EndShadowPass();
    void SetLatitude(float latitude);
    void SetLightDate(int year, int month, int day);
    void SetLightTimeMinutes(int minutes);
    glm::vec3 GetSunDirection() const;
    bool IsSunAboveHorizon() const;
    void SetOrthographicEnabled(bool enabled);
    unsigned int GetShadowProgram() const override { return m_shadowProgram; }
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
    bool CreateDummyShadowMap();
    void DestroyDummyShadowMap();
    bool CreateShadowMap(uint32_t size);
    void DestroyShadowMap();
    bool EnsureShadowMapForAnalysis();
    bool TryAllocateShadowMap(uint32_t size);
    glm::mat4 ComputeLightViewProj() const;
    bool ShouldRenderShadows() const;
    void SetShadowMapStatus(const std::string& message);
    void BindShadowTexture();
    glm::vec3 ProjectToVirtualSphere(float nx, float ny) const;
    void ApplyConstrainedLocalRotation(const glm::vec3& localAxis, float angle);

    bool CreateDepthResources();
    void DestroyDepthResources();

    bool m_wireframeMode = false;
    bool m_grayEnabled = false;
    bool m_dyeEnabled = false;
    bool m_lightAnalysisEnabled = false;
    uint32_t m_shadowTextureSize = 2048;
    uint32_t m_allocatedShadowTextureSize = 0;
    bool m_shadowMapReady = false;
    bool m_shadowPassActive = false;
    bool m_shadowBoundsValid = false;
    glm::vec3 m_shadowBoundsMin = glm::vec3(-1.0f);
    glm::vec3 m_shadowBoundsMax = glm::vec3(1.0f);
    glm::mat4 m_lightViewProj = glm::mat4(1.0f);
    std::string m_shadowMapStatus;
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
    unsigned int m_shadowProgram = 0;
    unsigned int m_ubo = 0;
    unsigned int m_dummyShadowTexture = 0;
    unsigned int m_shadowTexture = 0;
    unsigned int m_shadowFbo = 0;
};

#endif //__GL_RENDER_H__
