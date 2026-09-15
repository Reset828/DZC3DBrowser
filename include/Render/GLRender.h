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


// OpenGL 后端：上下文、帧循环与资源操作。
class GLRender : public Render {
public:
    GLRender();
    ~GLRender() override;

    GLRender(const GLRender&) = delete;
    GLRender& operator=(const GLRender&) = delete;

    // 初始化渲染器及其后端资源。
    bool Initialize(const char* appName, uint32_t width, uint32_t height) override;
    // 关闭渲染器并释放资源。
    void Shutdown() override;
    // 等待异步任务完成并使渲染器静止。
    void Quiesce() override;
    // 开始一帧渲染。
    bool BeginFrame() override;
    // 结束当前帧并提交结果。
    void EndFrame() override;
    // 提交索引绘制命令。
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) override;
    // 切换填充/线框多边形模式。
    void SetPolygonWireframe(bool enabled);

    // 返回 OpenGL 4.2 函数表。
    QOpenGLFunctions_4_2_Core* GetFunctions() const;
    // 返回当前着色器程序。
    unsigned int GetCurrentProgram() const;
    // 返回阴影着色器程序。
    virtual unsigned int GetShadowProgram() const { return 0; }

    // 等待 GPU 与异步任务完成。
    void WaitForIdle() override;
    // 把任务丢进线程池异步执行。
    void SubmitAsync(Render::AsyncTask task) override;
    // 把任务丢进线程池异步执行。
    void SubmitAsync(QRunnable* task);
    // 设置 Qt OpenGL 上下文。
    void SetContext(QOpenGLContext* context);
    // 设置渲染用 QWindow。
    void SetWindow(QWindow* window);
    // 返回 Qt OpenGL 上下文。
    QOpenGLContext* GetContext() const;
    // 返回渲染窗口。
    QWindow* GetWindow() const;
protected:
    // 后端初始化完成后的钩子。
    virtual bool OnInitialize() { return true; }
    // 关闭前释放后端资源的钩子。
    virtual void OnShutdown() {}

    // 每帧开始时的钩子。
    virtual void OnBeginFrame() {}
    // 每帧结束时的钩子。
    virtual void OnEndFrame() {}

    // 交换链/帧缓冲重建后的钩子。
    virtual void OnRecreateSwapchain() {}

    // 创建渲染通道。
    virtual bool CreateRenderPass();
    // 创建图形管线。
    virtual bool CreatePipelines();
    // 创建帧缓冲。
    virtual bool CreateFramebuffers();

    // 读取着色器文件。
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

    // 处理鼠标按下。
    void OnMouseDown(float nx, float ny, int button) override;
    // 处理鼠标移动。
    void OnMouseMove(float nx, float ny) override;
    // 处理鼠标松开。
    void OnMouseUp(int button) override;
    // 处理滚轮缩放。
    void OnMouseWheel(float delta) override;

protected:
    // 后端初始化完成后的钩子。
    bool OnInitialize() override;
    // 关闭前释放后端资源的钩子。
    void OnShutdown() override;
    // 每帧开始时的钩子。
    void OnBeginFrame() override;
    // 每帧结束时的钩子。
    void OnEndFrame() override;
    // 创建图形管线。
    bool CreatePipelines() override;

    // 创建描述符集布局。
    bool CreateDescriptorSetLayout();
    // 创建并映射 UBO。
    bool CreateUniformBuffers();
    // 创建描述符池。
    bool CreateDescriptorPool();
    // 分配并写入描述符集。
    bool CreateDescriptorSets();
    // 销毁 UBO 及其内存。
    void DestroyUniformBuffers();
    // 写入二维相机 UBO。
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

    // 处理鼠标按下。
    void OnMouseDown(float nx, float ny, int button) override;
    // 处理鼠标移动。
    void OnMouseMove(float nx, float ny) override;
    // 处理鼠标松开。
    void OnMouseUp(int button) override;
    // 处理滚轮缩放。
    void OnMouseWheel(float delta) override;
    // 开关线框模式。
    void SetWireframeEnabled(bool enabled);
    // 开关灰度显示。
    void SetGrayEnabled(bool enabled);
    // 开关染色。
    void SetDyeEnabled(bool enabled);
    // 开关光照分析。
    void SetLightAnalysisEnabled(bool enabled);
    // 设置阴影贴图边长。
    void SetShadowTextureSize(uint32_t size);
    // 返回阴影贴图边长。
    uint32_t GetShadowTextureSize() const;
    // 查询阴影贴图是否可用。
    bool IsShadowMapReady() const;
    // 读取并清除阴影贴图状态。
    std::string TakeShadowMapStatus();
    // 设置阴影场景包围盒。
    void SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid);
    // 开始向阴影贴图绘制。
    bool BeginShadowPass();
    // 结束阴影通道并恢复主帧缓冲。
    void EndShadowPass();
    // 设置太阳计算用纬度。
    void SetLatitude(float latitude);
    // 设置太阳计算用日期。
    void SetLightDate(int year, int month, int day);
    // 设置真太阳时（分钟）。
    void SetLightTimeMinutes(int minutes);
    // 返回物体空间太阳方向。
    glm::vec3 GetSunDirection() const;
    // 查询太阳是否在地平线以上。
    bool IsSunAboveHorizon() const;
    // 开关正射投影。
    void SetOrthographicEnabled(bool enabled);
    // 返回阴影着色器程序。
    unsigned int GetShadowProgram() const override { return m_shadowProgram; }
    // 设置轨道旋转中心。
    void SetOrbitCenter(const Vec3& normalizedCenter);
    // 复位旋转、平移和轨道距离。
    void ResetView(float orbitDistance = 3.0f);
    // 设置归一化坐标到世界坐标的变换。
    void SetCoordinateNormalization(const Vec3& sourceCenter, float normalizationScale);
    // 查询是否开启线框。
    bool IsWireframeEnabled() const override { return m_wireframeMode; }

    // 请求把屏幕点反算为世界坐标。
    void RequestCoordReadback(float ndcX, float ndcY);
    // 查询是否有新的世界坐标可读。
    bool HasNewWorldCoord() const;
    // 返回最近一次世界坐标 X。
    float GetLastWorldX() const;
    // 返回最近一次世界坐标 Y。
    float GetLastWorldY() const;
    // 返回最近一次世界坐标 Z。
    float GetLastWorldZ() const;

protected:
    // 后端初始化完成后的钩子。
    bool OnInitialize() override;
    // 关闭前释放后端资源的钩子。
    void OnShutdown() override;
    // 每帧开始时的钩子。
    void OnBeginFrame() override;
    // 每帧结束时的钩子。
    void OnEndFrame() override;
    // 交换链/帧缓冲重建后的钩子。
    void OnRecreateSwapchain() override;

    // 创建渲染通道。
    bool CreateRenderPass() override;
    // 创建图形管线。
    bool CreatePipelines() override;
    // 创建帧缓冲。
    bool CreateFramebuffers() override;

    // 编译并链接着色器程序。
    bool CreateShaderProgram();
    // 删除着色器程序。
    void DestroyShaderProgram();
    // 创建并映射 UBO。
    bool CreateUniformBuffers();
    // 销毁 UBO 及其内存。
    void DestroyUniformBuffers();
    // 按当前相机与显示选项写入 UBO。
    void UpdateUniformBuffer();
    // 编译 OpenGL 着色器。
    unsigned int CompileShader(unsigned int type, const char* source);
    // 链接 OpenGL 着色器程序。
    unsigned int LinkProgram(unsigned int vert, unsigned int frag);
    // 按纬度/日期/真太阳时更新太阳方向。
    void UpdateSunDirection();
    // 创建 1x1 占位阴影贴图。
    bool CreateDummyShadowMap();
    // 销毁占位阴影贴图。
    void DestroyDummyShadowMap();
    // 按给定边长创建阴影深度贴图。
    bool CreateShadowMap(uint32_t size);
    // 销毁阴影贴图与帧缓冲。
    void DestroyShadowMap();
    // 光照分析开启时保证阴影贴图可用。
    bool EnsureShadowMapForAnalysis();
    // 尝试分配指定尺寸的阴影贴图。
    bool TryAllocateShadowMap(uint32_t size);
    // 计算光源视图投影矩阵。
    glm::mat4 ComputeLightViewProj() const;
    // 判断当前是否需要渲染阴影。
    bool ShouldRenderShadows() const;
    // 记录阴影贴图状态文本。
    void SetShadowMapStatus(const std::string& message);
    // 绑定阴影贴图。
    void BindShadowTexture();
    // 将鼠标位置映射到虚拟球面。
    glm::vec3 ProjectToVirtualSphere(float nx, float ny) const;
    // 应用受约束的局部旋转。
    void ApplyConstrainedLocalRotation(const glm::vec3& localAxis, float angle);

    // 创建深度附件。
    bool CreateDepthResources();
    // 销毁深度附件。
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

    // 创建深度回读缓冲。
    bool CreateDepthReadbackResources();
    // 销毁深度回读缓冲。
    void DestroyDepthReadbackResources();
    // 把回读深度反投影为世界坐标。
    void ProcessDepthReadback();

    bool m_depthReadbackRequested = false;
    float m_requestedNDCX = 0.0f;
    float m_requestedNDCY = 0.0f;
    float m_lastWorldCoord[3] = {};
    mutable bool m_newCoordAvailable = false;
    glm::mat4 m_invViewProj = glm::mat4(1.0f);
    glm::mat4 m_renderToSource = glm::mat4(1.0f);
    glm::mat4 m_normalizedToWorld = glm::mat4(1.0f);

    unsigned int m_program = 0;
    unsigned int m_shadowProgram = 0;
    unsigned int m_ubo = 0;
    unsigned int m_dummyShadowTexture = 0;
    unsigned int m_shadowTexture = 0;
    unsigned int m_shadowFbo = 0;
};

#endif //__GL_RENDER_H__
