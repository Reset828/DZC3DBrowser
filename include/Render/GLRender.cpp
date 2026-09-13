#include "GLRender.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <QRunnable>
#include <QThreadPool>
#include <QCoreApplication>
#include <QEventLoop>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_2_Core>
#include <QWindow>
#include <utility>

namespace {

class GLFunctionRunnable final : public QRunnable {
public:
    explicit GLFunctionRunnable(Render::AsyncTask task)
        : m_task(std::move(task)) {}

    // 执行后台任务。
    void run() override {
        if (m_task) m_task();
    }

private:
    Render::AsyncTask m_task;
};

}

GLRender::GLRender() {}

GLRender::~GLRender() {
    Shutdown();
}

// 初始化渲染器及其后端资源。
bool GLRender::Initialize(const char* /*appName*/, uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;
    m_shuttingDown = false;

    if (!m_context || !m_window) return false;
    if (!m_context->makeCurrent(m_window)) return false;

    m_functions = m_context->versionFunctions<QOpenGLFunctions_4_2_Core>();
    if (!m_functions) return false;
    m_functions->initializeOpenGLFunctions();

    if (!OnInitialize()) return false;
    if (!CreateRenderPass()) return false;
    if (!CreatePipelines()) return false;
    if (!CreateFramebuffers()) return false;

    m_initialized = true;
    return true;
}

// 等待异步任务完成并使渲染器静止。
void GLRender::Quiesce() {
    if (!m_initialized) return;

    m_shuttingDown = true;
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// 关闭渲染器并释放资源。
void GLRender::Shutdown() {
    if (!m_initialized) return;

    Quiesce();
    if (m_context && m_window) {
        m_context->makeCurrent(m_window);
    }
    OnShutdown();
    if (m_context) {
        m_context->doneCurrent();
    }
    m_functions = nullptr;
    m_currentProgram = 0;
    m_initialized = false;
}


// 开始一帧渲染。
bool GLRender::BeginFrame() {
    if (!m_initialized || !m_context || !m_window) return false;
    if (!m_context->makeCurrent(m_window)) return false;

    if (!m_functions) return false;

    m_functions->glViewport(0, 0,
        static_cast<int>(m_framebufferWidth),
        static_cast<int>(m_framebufferHeight));
    m_functions->glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
    m_functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    OnBeginFrame();
    return true;
}

// 结束当前帧并提交结果。
void GLRender::EndFrame() {
    if (!m_initialized || !m_context || !m_window) return;
    OnEndFrame();
    m_context->swapBuffers(m_window);
}

// 提交索引绘制命令。
void GLRender::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    if (!m_functions || indexCount == 0) return;
    m_functions->glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indexCount),
        GL_UNSIGNED_INT, nullptr, static_cast<int>(instanceCount));
}

// 切换填充/线框多边形模式。
void GLRender::SetPolygonWireframe(bool enabled) {
    if (!m_functions) return;
    m_functions->glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
}

// 返回 OpenGL 4.2 函数表。
QOpenGLFunctions_4_2_Core* GLRender::GetFunctions() const { return m_functions; }
// 返回当前着色器程序。
unsigned int GLRender::GetCurrentProgram() const { return m_currentProgram; }

// 等待 GPU 与异步任务完成。
void GLRender::WaitForIdle() {}

// 把任务丢进线程池异步执行。
void GLRender::SubmitAsync(Render::AsyncTask task) {
    if (!task || IsShuttingDown()) return;
    QThreadPool::globalInstance()->start(new GLFunctionRunnable(std::move(task)));
}

// 把任务丢进线程池异步执行。
void GLRender::SubmitAsync(QRunnable* task) {
    if (!task || IsShuttingDown()) return;
    QThreadPool::globalInstance()->start(task);
}



// 设置 Qt OpenGL 上下文。
void GLRender::SetContext(QOpenGLContext* context) {
    m_context = context;
}

// 设置渲染用 QWindow。
void GLRender::SetWindow(QWindow* window) {
    m_window = window;
}

// 返回 Qt OpenGL 上下文。
QOpenGLContext* GLRender::GetContext() const { return m_context; }
// 返回渲染窗口。
QWindow* GLRender::GetWindow() const { return m_window; }











// 创建渲染通道。
bool GLRender::CreateRenderPass() { return true; }
// 创建图形管线。
bool GLRender::CreatePipelines() { return true; }
// 创建帧缓冲。
bool GLRender::CreateFramebuffers() { return true; }

// 读取着色器文件。
std::vector<char> GLRender::ReadShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开着色器文件: " + filename);
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();
    return buffer;
}

#include <glm/gtc/matrix_transform.hpp>

GLRender2D::GLRender2D() {}

GLRender2D::~GLRender2D() {}

// 处理鼠标按下。
void GLRender2D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

// 处理鼠标移动。
void GLRender2D::OnMouseMove(float nx, float ny) {
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

// 处理鼠标松开。
void GLRender2D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

// 处理滚轮缩放。
void GLRender2D::OnMouseWheel(float delta) {
    m_zoomLevel *= (delta > 0.0f) ? 0.85f : 1.18f;
    m_zoomLevel = glm::clamp(m_zoomLevel, 0.01f, 100.0f);
}

// 后端初始化完成后的钩子。
bool GLRender2D::OnInitialize() {
    return true;
}

// 关闭前释放后端资源的钩子。
void GLRender2D::OnShutdown() {
    DestroyUniformBuffers();
}

// 每帧开始时的钩子。
void GLRender2D::OnBeginFrame() {
    UpdateCameraUBO();
}

// 每帧结束时的钩子。
void GLRender2D::OnEndFrame() {}

// 创建图形管线。
bool GLRender2D::CreatePipelines() { return true; }
// 创建描述符集布局。
bool GLRender2D::CreateDescriptorSetLayout() { return true; }
// 创建并映射 UBO。
bool GLRender2D::CreateUniformBuffers() { return true; }
// 创建描述符池。
bool GLRender2D::CreateDescriptorPool() { return true; }
// 分配并写入描述符集。
bool GLRender2D::CreateDescriptorSets() { return true; }
// 销毁 UBO 及其内存。
void GLRender2D::DestroyUniformBuffers() {}
// 写入二维相机 UBO。
void GLRender2D::UpdateCameraUBO() {}

#include "Light/SolarPosition.h"
#include "VertexType/VertexTypes.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <QOpenGLFunctions_4_2_Core>
#include <QOpenGLContext>
#include <QWindow>
#include <iostream>
#include <string>

GLRender3D::GLRender3D() {
    UpdateSunDirection();
}

GLRender3D::~GLRender3D() {}

// 处理鼠标按下。
void GLRender3D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

// 处理鼠标移动。
void GLRender3D::OnMouseMove(float nx, float ny) {
    if (m_mouseButton < 0) return;
    const glm::vec2 previousMouse = m_lastMouse;
    const glm::vec3 sphereFrom =
        ProjectToVirtualSphere(previousMouse.x, previousMouse.y);
    const glm::vec3 sphereTo = ProjectToVirtualSphere(nx, ny);
    m_lastMouse = glm::vec2(nx, ny);

    if (m_mouseButton == 0) {
        if (m_orthographicEnabled) {
            const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
            const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
            const float minExtent = std::min(width, height);
            const float horizontalDelta =
                (nx - previousMouse.x) * width / minExtent;
            if (std::abs(horizontalDelta) <=
                std::numeric_limits<float>::epsilon()) {
                return;
            }

            constexpr float rotationSensitivity = 4.71238898038f;
            ApplyConstrainedLocalRotation(
                glm::vec3(0.0f, 0.0f, 1.0f),
                horizontalDelta * rotationSensitivity);
            return;
        }

        const glm::vec3 sphereCross = glm::cross(sphereFrom, sphereTo);
        const float sinAngle = glm::length(sphereCross);
        if (sinAngle <= std::numeric_limits<float>::epsilon()) return;

        const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
        const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
        const float minExtent = std::min(width, height);
        const glm::vec2 screenDelta(
            (nx - previousMouse.x) * width / minExtent,
            (ny - previousMouse.y) * height / minExtent);
        constexpr float rotationSensitivity = 4.71238898038f;
        const float uniformAngle = glm::length(screenDelta) * rotationSensitivity;

        glm::vec2 rotationAxisXY(sphereCross.x, sphereCross.y);
        if (glm::length(rotationAxisXY) <= 1.0e-6f) {
            rotationAxisXY = glm::vec2(screenDelta.y, screenDelta.x);
        }
        rotationAxisXY = glm::normalize(rotationAxisXY);
        const glm::vec3 sphereRotation(
            rotationAxisXY.x * uniformAngle,
            rotationAxisXY.y * uniformAngle,
            0.0f);

        if (std::abs(sphereRotation.y) >
            std::numeric_limits<float>::epsilon()) {
            ApplyConstrainedLocalRotation(
                glm::vec3(0.0f, 0.0f, 1.0f), sphereRotation.y);
        }

        if (std::abs(sphereRotation.x) >
            std::numeric_limits<float>::epsilon()) {
            const glm::vec3 worldLocalX =
                m_modelRotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 worldLocalY =
                m_modelRotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec2 localWeights(worldLocalX.x, worldLocalY.x);

            glm::vec3 verticalLocalAxis = m_lastVerticalLocalAxis;
            if (glm::length(localWeights) > 1.0e-4f) {
                verticalLocalAxis = glm::normalize(
                    glm::vec3(localWeights.x, localWeights.y, 0.0f));
                m_lastVerticalLocalAxis = verticalLocalAxis;
            }

            ApplyConstrainedLocalRotation(verticalLocalAxis, sphereRotation.x);
        }
    } else if (m_mouseButton == 2) {
        const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
        const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
        const float aspect = width / height;
        const float visibleHeight = 2.0f * m_orbitDistance
            * std::tan(glm::radians(45.0f) * 0.5f);
        const float visibleWidth = visibleHeight * aspect;

        const glm::vec2 mouseDelta = glm::vec2(nx, ny) - previousMouse;
        m_panOffset.x += mouseDelta.x * visibleWidth;
        m_panOffset.y -= mouseDelta.y * visibleHeight;
    }
}

// 将鼠标位置映射到虚拟球面。
glm::vec3 GLRender3D::ProjectToVirtualSphere(float nx, float ny) const {
    const float width = static_cast<float>(std::max(1u, m_framebufferWidth));
    const float height = static_cast<float>(std::max(1u, m_framebufferHeight));
    const float minExtent = std::min(width, height);
    float x = (nx * 2.0f - 1.0f) * width / minExtent;
    float y = (1.0f - ny * 2.0f) * height / minExtent;

    const float distanceSquared = x * x + y * y;
    const float distance = std::sqrt(distanceSquared);
    constexpr float sphereToHyperbola = 0.70710678118f;

    float z;
    if (distance <= sphereToHyperbola) {
        z = std::sqrt(1.0f - distanceSquared);
    } else {
        z = 0.5f / distance;
    }

    return glm::normalize(glm::vec3(x, y, z));
}

// 应用受约束的局部旋转。
void GLRender3D::ApplyConstrainedLocalRotation(const glm::vec3& localAxis,
                                                    float angle) {
    if (std::abs(angle) <= std::numeric_limits<float>::epsilon()) return;

    auto rotationAt = [&](float ratio) {
        return glm::normalize(m_modelRotation * glm::angleAxis(angle * ratio, localAxis));
    };
    auto zAxisDoesNotPointDown = [](const glm::quat& rotation) {
        const glm::vec3 worldZ = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        return worldZ.y >= -1.0e-6f;
    };

    glm::quat candidate = rotationAt(1.0f);
    if (zAxisDoesNotPointDown(candidate)) {
        m_modelRotation = candidate;
        return;
    }

    float allowed = 0.0f;
    float rejected = 1.0f;
    for (int i = 0; i < 16; ++i) {
        const float middle = (allowed + rejected) * 0.5f;
        if (zAxisDoesNotPointDown(rotationAt(middle))) {
            allowed = middle;
        } else {
            rejected = middle;
        }
    }
    m_modelRotation = rotationAt(allowed);
}

// 处理鼠标松开。
void GLRender3D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

// 处理滚轮缩放。
void GLRender3D::OnMouseWheel(float delta) {
    m_orbitDistance *= (delta > 0.0f) ? 0.9f : 1.1f;
    m_orbitDistance = glm::clamp(m_orbitDistance, 0.1f, 1000.0f);
}

// 后端初始化完成后的钩子。
bool GLRender3D::OnInitialize() {
    if (!CreateShaderProgram()) return false;
    if (!CreateUniformBuffers()) return false;
    if (!CreateDummyShadowMap()) return false;
    if (m_functions) {
        m_functions->glEnable(GL_DEPTH_TEST);
        m_functions->glDepthFunc(GL_LESS);
        m_functions->glDisable(GL_CULL_FACE);
    }
    if (m_lightAnalysisEnabled) {
        EnsureShadowMapForAnalysis();
    }
    return true;
}

// 关闭前释放后端资源的钩子。
void GLRender3D::OnShutdown() {
    DestroyShadowMap();
    DestroyDummyShadowMap();
    DestroyDepthReadbackResources();
    DestroyUniformBuffers();
    DestroyShaderProgram();
    DestroyDepthResources();
}

// 每帧开始时的钩子。
void GLRender3D::OnBeginFrame() {
    ProcessDepthReadback();
    if (m_functions && m_program != 0) {
        m_functions->glUseProgram(m_program);
        m_currentProgram = m_program;
        m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    }
    BindShadowTexture();
    UpdateUniformBuffer();
}

// 每帧结束时的钩子。
void GLRender3D::OnEndFrame() {}

// 交换链/帧缓冲重建后的钩子。
void GLRender3D::OnRecreateSwapchain() {
    DestroyDepthResources();
    CreateDepthResources();
}

// 创建渲染通道。
bool GLRender3D::CreateRenderPass() { return true; }
// 创建图形管线。
bool GLRender3D::CreatePipelines() { return true; }
// 创建帧缓冲。
bool GLRender3D::CreateFramebuffers() { return true; }

// 编译 OpenGL 着色器。
unsigned int GLRender3D::CompileShader(unsigned int type, const char* source) {
    if (!m_functions) return 0;
    unsigned int shader = m_functions->glCreateShader(type);
    m_functions->glShaderSource(shader, 1, &source, nullptr);
    m_functions->glCompileShader(shader);
    int success = 0;
    m_functions->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int logLength = 0;
        m_functions->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) {
            std::vector<char> log(static_cast<size_t>(logLength), '\0');
            m_functions->glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            std::cerr << "OpenGL 着色器编译失败: " << log.data() << std::endl;
        }
        m_functions->glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// 链接 OpenGL 着色器程序。
unsigned int GLRender3D::LinkProgram(unsigned int vert, unsigned int frag) {
    if (!m_functions) return 0;
    unsigned int program = m_functions->glCreateProgram();
    m_functions->glAttachShader(program, vert);
    m_functions->glAttachShader(program, frag);
    m_functions->glLinkProgram(program);
    int success = 0;
    m_functions->glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        int logLength = 0;
        m_functions->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) {
            std::vector<char> log(static_cast<size_t>(logLength), '\0');
            m_functions->glGetProgramInfoLog(program, logLength, nullptr, log.data());
            std::cerr << "OpenGL 程序链接失败: " << log.data() << std::endl;
        }
        m_functions->glDeleteProgram(program);
        return 0;
    }
    return program;
}

// 编译并链接着色器程序。
bool GLRender3D::CreateShaderProgram() {
    if (!m_functions) return false;
    try {
        const std::vector<char> vertCode = ReadShaderFile("res/3d.vert");
        const std::vector<char> fragCode = ReadShaderFile("res/3d.frag");
        std::string vertSource(vertCode.begin(), vertCode.end());
        std::string fragSource(fragCode.begin(), fragCode.end());
        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSource.c_str());
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
        if (vert == 0 || frag == 0) {
            if (vert) m_functions->glDeleteShader(vert);
            if (frag) m_functions->glDeleteShader(frag);
            return false;
        }
        m_program = LinkProgram(vert, frag);
        m_functions->glDeleteShader(vert);
        m_functions->glDeleteShader(frag);
        if (m_program == 0) return false;

        const std::vector<char> shadowVertCode = ReadShaderFile("res/3d_shadow.vert");
        const std::vector<char> shadowFragCode = ReadShaderFile("res/3d_shadow.frag");
        std::string shadowVertSource(shadowVertCode.begin(), shadowVertCode.end());
        std::string shadowFragSource(shadowFragCode.begin(), shadowFragCode.end());
        unsigned int shadowVert = CompileShader(GL_VERTEX_SHADER, shadowVertSource.c_str());
        unsigned int shadowFrag = CompileShader(GL_FRAGMENT_SHADER, shadowFragSource.c_str());
        if (shadowVert == 0 || shadowFrag == 0) {
            if (shadowVert) m_functions->glDeleteShader(shadowVert);
            if (shadowFrag) m_functions->glDeleteShader(shadowFrag);
            return false;
        }
        m_shadowProgram = LinkProgram(shadowVert, shadowFrag);
        m_functions->glDeleteShader(shadowVert);
        m_functions->glDeleteShader(shadowFrag);
        return m_shadowProgram != 0;
    } catch (const std::exception& e) {
        std::cerr << "OpenGL 读取着色器失败: " << e.what() << std::endl;
        return false;
    }
}

// 删除着色器程序。
void GLRender3D::DestroyShaderProgram() {
    if (m_functions && m_program != 0) {
        m_functions->glDeleteProgram(m_program);
    }
    if (m_functions && m_shadowProgram != 0) {
        m_functions->glDeleteProgram(m_shadowProgram);
    }
    m_program = 0;
    m_shadowProgram = 0;
    m_currentProgram = 0;
}

// 创建并映射 UBO。
bool GLRender3D::CreateUniformBuffers() {
    if (!m_functions) return false;
    m_functions->glGenBuffers(1, &m_ubo);
    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    m_functions->glBufferData(GL_UNIFORM_BUFFER, sizeof(UniformBufferObject3D), nullptr, GL_DYNAMIC_DRAW);
    m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    unsigned int blockIndex = m_functions->glGetUniformBlockIndex(m_program, "UniformBufferObject");
    if (blockIndex != GL_INVALID_INDEX) {
        m_functions->glUniformBlockBinding(m_program, blockIndex, 0);
    }
    unsigned int shadowBlockIndex = m_functions->glGetUniformBlockIndex(m_shadowProgram, "UniformBufferObject");
    if (shadowBlockIndex != GL_INVALID_INDEX) {
        m_functions->glUniformBlockBinding(m_shadowProgram, shadowBlockIndex, 0);
    }
    int shadowMapLocation = m_functions->glGetUniformLocation(m_program, "shadowMap");
    if (shadowMapLocation >= 0) {
        m_functions->glUseProgram(m_program);
        m_functions->glUniform1i(shadowMapLocation, 1);
        m_functions->glUseProgram(0);
    }
    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return true;
}

// 销毁 UBO 及其内存。
void GLRender3D::DestroyUniformBuffers() {
    if (m_functions && m_ubo != 0) {
        m_functions->glDeleteBuffers(1, &m_ubo);
    }
    m_ubo = 0;
}

// 按当前相机与显示选项写入 UBO。
void GLRender3D::UpdateUniformBuffer() {
    if (!m_functions || m_ubo == 0) return;

    float aspect = (float)m_framebufferWidth / (float)m_framebufferHeight;

    const glm::vec3 eye(0.0f, 0.0f, m_orbitDistance);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), m_panOffset)
        * glm::mat4_cast(m_modelRotation)
        * glm::translate(glm::mat4(1.0f), -m_orbitCenter);
    const glm::mat4 view = glm::lookAt(
        eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const float verticalFov = glm::radians(45.0f);
    glm::mat4 proj;
    if (m_orthographicEnabled) {
        const float halfHeight = m_orbitDistance * std::tan(verticalFov * 0.5f);
        const float halfWidth = halfHeight * aspect;
        proj = glm::orthoRH_NO(-halfWidth, halfWidth,
                               -halfHeight, halfHeight,
                               0.1f, 100.0f);
    } else {
        proj = glm::perspectiveRH_NO(verticalFov, aspect, 0.1f, 100.0f);
    }

    UniformBufferObject3D ubo{};
    memcpy(ubo.model, glm::value_ptr(model), sizeof(float) * 16);
    memcpy(ubo.view, glm::value_ptr(view), sizeof(float) * 16);
    memcpy(ubo.proj, glm::value_ptr(proj), sizeof(float) * 16);
    ubo.displayOptions[0] = m_grayEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[1] = m_dyeEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[2] = m_lightAnalysisEnabled ? 1.0f : 0.0f;
    ubo.displayOptions[3] = m_sunAboveHorizon ? 1.0f : 0.0f;
    ubo.sunDirection[0] = m_sunDirection.x;
    ubo.sunDirection[1] = m_sunDirection.y;
    ubo.sunDirection[2] = m_sunDirection.z;
    ubo.sunDirection[3] = 0.0f;
    m_lightViewProj = ComputeLightViewProj();
    memcpy(ubo.lightViewProj, glm::value_ptr(m_lightViewProj), sizeof(float) * 16);
    ubo.shadowOptions[0] = ShouldRenderShadows() ? 1.0f : 0.0f;
    ubo.shadowOptions[1] = 1.0f;
    ubo.shadowOptions[2] = m_wireframeMode ? 1.0f : 0.0f;
    ubo.shadowOptions[3] = 0.0f;

    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    m_functions->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
// 创建深度附件。
bool GLRender3D::CreateDepthResources() { return true; }
// 销毁深度附件。
void GLRender3D::DestroyDepthResources() {}
// 创建深度回读缓冲。
bool GLRender3D::CreateDepthReadbackResources() { return true; }
// 销毁深度回读缓冲。
void GLRender3D::DestroyDepthReadbackResources() {}
// 把回读深度反投影为世界坐标。
void GLRender3D::ProcessDepthReadback() {}

// 开关线框模式。
void GLRender3D::SetWireframeEnabled(bool enabled) {
    m_wireframeMode = enabled;
}

// 开关灰度显示。
void GLRender3D::SetGrayEnabled(bool enabled) {
    m_grayEnabled = enabled;
}

// 开关染色。
void GLRender3D::SetDyeEnabled(bool enabled) {
    m_dyeEnabled = enabled;
}

// 开关光照分析。
void GLRender3D::SetLightAnalysisEnabled(bool enabled) {
    m_lightAnalysisEnabled = enabled;
    if (!m_initialized) return;
    if (m_context && m_window) {
        m_context->makeCurrent(m_window);
    }
    if (!enabled) {
        DestroyShadowMap();
        m_shadowMapReady = false;
        m_allocatedShadowTextureSize = 0;
        return;
    }
    EnsureShadowMapForAnalysis();
}

// 设置阴影贴图边长。
void GLRender3D::SetShadowTextureSize(uint32_t size) {
    m_shadowTextureSize = size;
    if (m_initialized && m_lightAnalysisEnabled) {
        if (m_context && m_window) {
            m_context->makeCurrent(m_window);
        }
        EnsureShadowMapForAnalysis();
    }
}

// 获取阴影贴图尺寸。
uint32_t GLRender3D::GetShadowTextureSize() const {
    return m_shadowTextureSize;
}

// 查询阴影贴图是否就绪。
bool GLRender3D::IsShadowMapReady() const {
    return m_shadowMapReady;
}

// 读取并清除阴影贴图状态。
std::string GLRender3D::TakeShadowMapStatus() {
    std::string message = std::move(m_shadowMapStatus);
    m_shadowMapStatus.clear();
    return message;
}

// 设置阴影场景包围盒。
void GLRender3D::SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid) {
    m_shadowBoundsValid = valid;
    m_shadowBoundsMin = glm::vec3(boundsMin.x, boundsMin.y, boundsMin.z);
    m_shadowBoundsMax = glm::vec3(boundsMax.x, boundsMax.y, boundsMax.z);
}

// 记录阴影贴图状态文本。
void GLRender3D::SetShadowMapStatus(const std::string& message) {
    m_shadowMapStatus = message;
}

// 判断当前是否应渲染阴影。
bool GLRender3D::ShouldRenderShadows() const {
    return m_lightAnalysisEnabled && m_sunAboveHorizon && m_shadowMapReady && !m_shadowPassActive;
}

// 计算光源视图投影矩阵。
glm::mat4 GLRender3D::ComputeLightViewProj() const {
    glm::vec3 boundsMin = m_shadowBoundsMin;
    glm::vec3 boundsMax = m_shadowBoundsMax;
    if (!m_shadowBoundsValid) {
        boundsMin = glm::vec3(-1.0f);
        boundsMax = glm::vec3(1.0f);
    }

    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 extent = (boundsMax - boundsMin) * 0.5f * 1.1f;
    extent = glm::max(extent, glm::vec3(0.1f));

    glm::vec3 sun = m_sunDirection;
    const float sunLength = glm::length(sun);
    if (sunLength <= 1.0e-6f) {
        sun = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        sun /= sunLength;
    }

    glm::vec3 up(0.0f, 0.0f, 1.0f);
    if (std::abs(glm::dot(sun, up)) > 0.99f) {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const float radius = glm::length(extent);
    const glm::vec3 eye = center + sun * (radius + 0.1f);
    const glm::mat4 lightView = glm::lookAt(eye, center, up);

    glm::vec3 paddedMin = center - extent;
    glm::vec3 paddedMax = center + extent;
    glm::vec3 corners[8] = {
        { paddedMin.x, paddedMin.y, paddedMin.z },
        { paddedMax.x, paddedMin.y, paddedMin.z },
        { paddedMin.x, paddedMax.y, paddedMin.z },
        { paddedMax.x, paddedMax.y, paddedMin.z },
        { paddedMin.x, paddedMin.y, paddedMax.z },
        { paddedMax.x, paddedMin.y, paddedMax.z },
        { paddedMin.x, paddedMax.y, paddedMax.z },
        { paddedMax.x, paddedMax.y, paddedMax.z }
    };

    glm::vec3 viewMin(std::numeric_limits<float>::max());
    glm::vec3 viewMax(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners) {
        const glm::vec3 viewPos = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        viewMin = glm::min(viewMin, viewPos);
        viewMax = glm::max(viewMax, viewPos);
    }

    float zNear = std::max(0.01f, -viewMax.z);
    float zFar = std::max(zNear + 0.01f, -viewMin.z);
    return glm::orthoRH_NO(viewMin.x, viewMax.x, viewMin.y, viewMax.y, zNear, zFar) * lightView;
}

// 绑定阴影贴图。
void GLRender3D::BindShadowTexture() {
    if (!m_functions) return;
    m_functions->glActiveTexture(GL_TEXTURE1);
    unsigned int texture = m_dummyShadowTexture;
    if (ShouldRenderShadows() && m_shadowTexture != 0) {
        texture = m_shadowTexture;
    }
    m_functions->glBindTexture(GL_TEXTURE_2D, texture);
    m_functions->glActiveTexture(GL_TEXTURE0);
}

// 创建 1x1 占位阴影贴图。
bool GLRender3D::CreateDummyShadowMap() {
    if (!m_functions) return false;
    m_functions->glGenTextures(1, &m_dummyShadowTexture);
    m_functions->glBindTexture(GL_TEXTURE_2D, m_dummyShadowTexture);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 1, 1, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_functions->glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    const float depth = 1.0f;
    m_functions->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    m_functions->glBindTexture(GL_TEXTURE_2D, 0);
    return m_dummyShadowTexture != 0;
}

// 销毁占位阴影贴图。
void GLRender3D::DestroyDummyShadowMap() {
    if (m_functions && m_dummyShadowTexture != 0) {
        m_functions->glDeleteTextures(1, &m_dummyShadowTexture);
    }
    m_dummyShadowTexture = 0;
}

// 按给定边长创建阴影深度贴图。
bool GLRender3D::CreateShadowMap(uint32_t size) {
    if (!m_functions || size == 0) return false;
    int maxSize = 0;
    m_functions->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (static_cast<int>(size) > maxSize) return false;

    m_functions->glGenTextures(1, &m_shadowTexture);
    m_functions->glBindTexture(GL_TEXTURE_2D, m_shadowTexture);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
        static_cast<int>(size), static_cast<int>(size), 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_functions->glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);

    m_functions->glGenFramebuffers(1, &m_shadowFbo);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    m_functions->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowTexture, 0);
    m_functions->glDrawBuffer(GL_NONE);
    m_functions->glReadBuffer(GL_NONE);
    const unsigned int status = m_functions->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_functions->glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DestroyShadowMap();
        return false;
    }
    const unsigned int error = m_functions->glGetError();
    if (error != GL_NO_ERROR) {
        DestroyShadowMap();
        return false;
    }
    return true;
}

// 销毁阴影贴图与帧缓冲。
void GLRender3D::DestroyShadowMap() {
    if (m_functions && m_shadowFbo != 0) {
        m_functions->glDeleteFramebuffers(1, &m_shadowFbo);
    }
    if (m_functions && m_shadowTexture != 0) {
        m_functions->glDeleteTextures(1, &m_shadowTexture);
    }
    m_shadowFbo = 0;
    m_shadowTexture = 0;
    m_shadowMapReady = false;
    m_allocatedShadowTextureSize = 0;
}

// 尝试分配指定尺寸的阴影贴图。
bool GLRender3D::TryAllocateShadowMap(uint32_t size) {
    DestroyShadowMap();
    if (!CreateShadowMap(size)) return false;
    m_shadowMapReady = true;
    m_allocatedShadowTextureSize = size;
    return true;
}

// 光照分析开启时保证阴影贴图可用。
bool GLRender3D::EnsureShadowMapForAnalysis() {
    if (!m_initialized || !m_lightAnalysisEnabled) return false;
    if (m_context && m_window) {
        m_context->makeCurrent(m_window);
    }
    if (m_shadowMapReady && m_allocatedShadowTextureSize == m_shadowTextureSize) {
        return true;
    }

    const uint32_t requested = m_shadowTextureSize;
    const uint32_t previous = m_allocatedShadowTextureSize;
    const bool hadPrevious = m_shadowMapReady && previous != 0;

    if (TryAllocateShadowMap(requested)) {
        return true;
    }

    if (hadPrevious && TryAllocateShadowMap(previous)) {
        m_shadowTextureSize = previous;
        SetShadowMapStatus("阴影贴图创建失败，已保留 " + std::to_string(previous));
        return true;
    }

    if (requested != 2048 && previous != 2048 && TryAllocateShadowMap(2048)) {
        m_shadowTextureSize = 2048;
        SetShadowMapStatus("阴影贴图创建失败，已改用 2048");
        return true;
    }

    m_shadowTextureSize = 2048;
    m_shadowMapReady = false;
    m_allocatedShadowTextureSize = 0;
    SetShadowMapStatus("阴影贴图创建失败");
    return false;
}

// 开始向阴影贴图绘制。
bool GLRender3D::BeginShadowPass() {
    if (!m_initialized || !m_lightAnalysisEnabled || !m_sunAboveHorizon) return false;
    if (!m_context || !m_window || !m_context->makeCurrent(m_window)) return false;
    if (!EnsureShadowMapForAnalysis() || !m_shadowMapReady || m_shadowFbo == 0) return false;
    if (!m_functions) return false;

    m_shadowPassActive = true;
    UpdateUniformBuffer();
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    m_functions->glViewport(0, 0,
        static_cast<int>(m_allocatedShadowTextureSize),
        static_cast<int>(m_allocatedShadowTextureSize));
    m_functions->glClear(GL_DEPTH_BUFFER_BIT);
    m_functions->glEnable(GL_POLYGON_OFFSET_FILL);
    m_functions->glPolygonOffset(1.75f, 1.25f);
    if (m_shadowProgram != 0) {
        m_functions->glUseProgram(m_shadowProgram);
        m_currentProgram = m_shadowProgram;
        m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    }
    return true;
}

// 结束阴影通道并恢复主帧缓冲。
void GLRender3D::EndShadowPass() {
    if (!m_shadowPassActive) return;
    if (m_functions) {
        m_functions->glDisable(GL_POLYGON_OFFSET_FILL);
        m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_functions->glViewport(0, 0,
            static_cast<int>(m_framebufferWidth),
            static_cast<int>(m_framebufferHeight));
    }
    m_shadowPassActive = false;
}

// 设置太阳计算用纬度。
void GLRender3D::SetLatitude(float latitude) {
    m_latitude = latitude;
    UpdateSunDirection();
}

// 设置太阳计算用日期。
void GLRender3D::SetLightDate(int year, int month, int day) {
    m_lightYear = year;
    m_lightMonth = month;
    m_lightDay = day;
    UpdateSunDirection();
}

// 设置真太阳时（分钟）。
void GLRender3D::SetLightTimeMinutes(int minutes) {
    m_lightTimeMinutes = minutes;
    UpdateSunDirection();
}

// 获取太阳光方向。
glm::vec3 GLRender3D::GetSunDirection() const {
    return m_sunDirection;
}

// 查询太阳是否位于地平线以上。
bool GLRender3D::IsSunAboveHorizon() const {
    return m_sunAboveHorizon;
}

// 按纬度/日期/真太阳时更新太阳方向。
void GLRender3D::UpdateSunDirection() {
    SolarPositionQuery query{};
    query.latitudeDegrees = m_latitude;
    query.year = m_lightYear;
    query.month = m_lightMonth;
    query.day = m_lightDay;
    query.trueSolarTimeHours = 6.0f + static_cast<float>(m_lightTimeMinutes) / 60.0f;

    const SolarPosition sun = ComputeSolarPosition(query);
    m_sunDirection = glm::vec3(sun.directionX, sun.directionY, sun.directionZ);
    m_sunAboveHorizon = sun.aboveHorizon;
}

// 开关正射投影。
void GLRender3D::SetOrthographicEnabled(bool enabled) {
    if (enabled && !m_orthographicEnabled) {
        m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        m_mouseButton = -1;
        m_lastMouse = glm::vec2(0.0f);
        m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    m_orthographicEnabled = enabled;
}

// 设置轨道旋转中心。
void GLRender3D::SetOrbitCenter(const Vec3& normalizedCenter) {
    m_orbitCenter = glm::vec3(
        normalizedCenter.x, normalizedCenter.y, normalizedCenter.z);
}

// 复位旋转、平移和轨道距离。
void GLRender3D::ResetView(float orbitDistance) {
    m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_panOffset = glm::vec3(0.0f);
    m_orbitDistance = std::isfinite(orbitDistance)
        ? glm::clamp(orbitDistance, 0.1f, 1000.0f)
        : 3.0f;
    m_mouseButton = -1;
    m_lastMouse = glm::vec2(0.0f);
    m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
}

// 设置归一化坐标到世界坐标的变换。
void GLRender3D::SetCoordinateNormalization(const Vec3& sourceCenter,
                                                 float normalizationScale) {
    if (!std::isfinite(normalizationScale) || normalizationScale <= 0.0f) {
        m_normalizedToWorld = glm::mat4(1.0f);
        return;
    }

    const glm::mat4 translateToSource = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(sourceCenter.x, sourceCenter.y, sourceCenter.z));
    const glm::mat4 undoScale = glm::scale(
        glm::mat4(1.0f), glm::vec3(1.0f / normalizationScale));
    m_normalizedToWorld = translateToSource * undoScale;
}

// 请求把屏幕点反算为世界坐标。
void GLRender3D::RequestCoordReadback(float ndcX, float ndcY) {
    m_depthReadbackRequested = true;
    m_requestedNDCX = ndcX;
    m_requestedNDCY = ndcY;
}

// 查询是否有新的世界坐标。
bool GLRender3D::HasNewWorldCoord() const {
    bool v = m_newCoordAvailable;
    m_newCoordAvailable = false;
    return v;
}

// 获取最近一次世界坐标的 X 分量。
float GLRender3D::GetLastWorldX() const { return m_lastWorldCoord[0]; }
// 获取最近一次世界坐标的 Y 分量。
float GLRender3D::GetLastWorldY() const { return m_lastWorldCoord[1]; }
// 获取最近一次世界坐标的 Z 分量。
float GLRender3D::GetLastWorldZ() const { return m_lastWorldCoord[2]; }
