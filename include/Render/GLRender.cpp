#include "GLRender.h"
#include "Path/AssetPath.h"
#include "Texture/GLTexture.h"
#include "Object/Object.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>
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
    m_deviceLost = false;
    m_lastError.clear();

    if (!m_context || !m_window) {
        SetLastError("OpenGL 上下文或窗口未就绪");
        return false;
    }
    if (!m_context->makeCurrent(m_window)) {
        SetLastError("OpenGL makeCurrent 失败");
        return false;
    }

    m_functions = m_context->versionFunctions<QOpenGLFunctions_4_2_Core>();
    if (!m_functions) {
        SetLastError("无法获取 OpenGL 4.2 Core 函数表");
        return false;
    }
    m_functions->initializeOpenGLFunctions();

    if (!OnInitialize()) {
        if (m_lastError.empty()) SetLastError("OpenGL 后端初始化失败");
        return false;
    }
    if (!CreateRenderPass()) {
        if (m_lastError.empty()) SetLastError("OpenGL 创建 RenderPass 失败");
        return false;
    }
    if (!CreatePipelines()) {
        if (m_lastError.empty()) SetLastError("OpenGL 创建管线失败");
        return false;
    }
    if (!CreateFramebuffers()) {
        if (m_lastError.empty()) SetLastError("OpenGL 创建 Framebuffer 失败");
        return false;
    }

    m_initialized = true;
    return true;
}

// 等待异步任务完成并使渲染器静止。
void GLRender::Quiesce() {
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
    // 释放 GPU 纹理（在上下文仍然有效时）。
    ReleaseAllTextures();
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
    if (!m_initialized || m_deviceLost || !m_context || !m_window) return false;
    if (m_framebufferWidth == 0 || m_framebufferHeight == 0) return false;
    if (!m_context->makeCurrent(m_window)) return false;

    if (!m_functions) return false;

    // 1.6：绑定帧缓冲前准备本帧渲染目标（HDR 开启时按当前尺寸重建 HDR 中间目标）。
    OnPrepareFrame();

    // 1.6：HDR 开启时把场景画进离屏 HDR 中间目标，否则回到默认帧缓冲。
    const unsigned int sceneFbo = GetSceneFramebuffer();
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
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
    if (!m_initialized || m_deviceLost || !m_context || !m_window) return;
    if (m_framebufferWidth == 0 || m_framebufferHeight == 0) return;
    // 主通道结束前刷新透明绘制（按深度排序）。
    FlushTransparentDraws();
    OnEndFrame();
    // 1.6：场景已画进 HDR 中间目标时，做后处理并输出到默认帧缓冲。
    OnAfterSceneRender();
    // 任务 2.4：场景/后处理结束后把 Gizmo 叠加到默认帧缓冲。
    OnOverlayPass();
    // 推进纹理延迟销毁队列（每帧一次）。
    ProcessDeferredTextureDestruction();
    m_context->swapBuffers(m_window);
}

// 创建一张 GPU 纹理（上传 + sRGB 内部格式 + mipmap）。
// 若 desc.cacheKey 非空且已存在，则复用同一纹理并增加引用计数。
TextureHandle GLRender::CreateTexture(const TextureDesc& desc) {
    if (!m_functions || !m_context || !m_window) return 0;
    if (!m_context->makeCurrent(m_window)) return 0;

    if (!desc.cacheKey.empty()) {
        const auto found = m_textureKeyToHandle.find(desc.cacheKey);
        if (found != m_textureKeyToHandle.end()) {
            ++m_textureRefCount[found->second];
            return found->second;
        }
    }

    auto texture = std::make_unique<GLTexture>();
    if (!texture->Create(m_functions, desc)) {
        return 0;
    }

    const TextureHandle handle = m_nextTextureHandle++;
    m_textures.emplace(handle, std::move(texture));
    m_textureRefCount[handle] = 1;
    if (!desc.cacheKey.empty()) {
        m_textureKeyToHandle.emplace(desc.cacheKey, handle);
    }
    return handle;
}

// 标记释放纹理：递减引用计数，归零才进入延迟销毁队列。
void GLRender::DestroyTexture(TextureHandle handle) {
    if (handle == 0) return;
    if (m_textures.find(handle) == m_textures.end()) return;

    auto ref = m_textureRefCount.find(handle);
    if (ref != m_textureRefCount.end() && --ref->second > 0) {
        return;
    }
    for (const auto& entry : m_deferredTextureDestruction) {
        if (entry.first == handle) return;
    }
    m_deferredTextureDestruction.emplace_back(handle, m_textureFrameCounter);
}

// 推进延迟销毁队列。
void GLRender::ProcessDeferredTextureDestruction() {
    ++m_textureFrameCounter;
    if (m_deferredTextureDestruction.empty()) return;
    if (!m_functions) return;

    const uint64_t safeFrame =
        m_textureFrameCounter > kTextureDestroyDelayFrames
            ? m_textureFrameCounter - kTextureDestroyDelayFrames : 0;

    for (auto it = m_deferredTextureDestruction.begin();
         it != m_deferredTextureDestruction.end();) {
        if (it->second <= safeFrame) {
            auto found = m_textures.find(it->first);
            if (found != m_textures.end()) {
                for (auto keyIt = m_textureKeyToHandle.begin();
                     keyIt != m_textureKeyToHandle.end();) {
                    if (keyIt->second == it->first) {
                        keyIt = m_textureKeyToHandle.erase(keyIt);
                    } else {
                        ++keyIt;
                    }
                }
                m_textureRefCount.erase(it->first);
                found->second->Destroy();
                m_textures.erase(found);
            }
            it = m_deferredTextureDestruction.erase(it);
        } else {
            ++it;
        }
    }
}

// 立即销毁全部纹理。
void GLRender::ReleaseAllTextures() {
    if (m_textures.empty() && m_deferredTextureDestruction.empty()) return;
    if (m_functions && m_context && m_window) {
        m_context->makeCurrent(m_window);
    }
    for (auto& entry : m_textures) {
        entry.second->Destroy();
    }
    m_textures.clear();
    m_textureKeyToHandle.clear();
    m_textureRefCount.clear();
    m_deferredTextureDestruction.clear();
}

// 查询句柄是否有效。
bool GLRender::IsTextureValid(TextureHandle handle) const {
    return handle != 0 && m_textures.find(handle) != m_textures.end();
}

// 返回纹理对象名。
unsigned int GLRender::GetTextureObject(TextureHandle handle) const {
    const auto found = m_textures.find(handle);
    return found == m_textures.end() ? 0 : found->second->GetTexture();
}

// 返回采样器对象名。
unsigned int GLRender::GetTextureSamplerObject(TextureHandle handle) const {
    const auto found = m_textures.find(handle);
    return found == m_textures.end() ? 0 : found->second->GetSampler();
}

// 提交索引绘制命令。
void GLRender::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    if (!m_functions || indexCount == 0) return;
    m_functions->glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indexCount),
        GL_UNSIGNED_INT, nullptr, static_cast<int>(instanceCount));
}

// 提交带起始索引的索引绘制（SubMesh 范围）。
void GLRender::DrawIndexedRange(uint32_t indexCount, uint32_t firstIndex,
                                uint32_t vertexOffset) {
    if (!m_functions || indexCount == 0) return;
    const void* offset = reinterpret_cast<const void*>(
        static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t));
    if (vertexOffset != 0) {
        // 顶点偏移用 glDrawElementsBaseVertex。
        m_functions->glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<int>(indexCount),
            GL_UNSIGNED_INT, offset, static_cast<int>(vertexOffset));
    } else {
        m_functions->glDrawElements(GL_TRIANGLES, static_cast<int>(indexCount),
            GL_UNSIGNED_INT, offset);
    }
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
#include <glm/gtc/matrix_inverse.hpp>
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
    if (!CreateShaderProgram()) {
        if (m_lastError.empty()) SetLastError("OpenGL 着色器程序创建失败");
        return false;
    }
    if (!CreateUniformBuffers()) {
        SetLastError("OpenGL 创建 UBO 失败");
        return false;
    }
    if (!CreateDummyShadowMap()) {
        SetLastError("OpenGL 创建占位阴影贴图失败");
        return false;
    }
    // HDR 后处理为可选功能：程序创建失败时记录但不中断（HDR 会自动降级为 LDR 直出）。
    if (!CreatePostProgram()) {
        std::cerr << "OpenGL: 后处理程序创建失败，HDR 将不可用" << std::endl;
    }
    // 任务 2.4：Gizmo 叠加程序为可选功能，失败仅记录（无 Gizmo 仍可正常渲染）。
    if (!CreateGizmoProgram()) {
        std::cerr << "OpenGL: Gizmo 程序创建失败，Gizmo 将不可用" << std::endl;
    }
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
    DestroyGizmoSupport();
    DestroyPostProgram();
    DestroyHdrResources();
    DestroyMsaaResources();
    DestroyShadowMap();
    DestroyDummyShadowMap();
    DestroyDepthReadbackResources();
    DestroyUniformBuffers();
    DestroyShaderProgram();
    DestroyDepthResources();
}

// 每帧开始时的钩子。
void GLRender3D::OnBeginFrame() {
    if (m_functions && m_program != 0) {
        m_functions->glUseProgram(m_program);
        m_currentProgram = m_program;
        m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    }
    BindShadowTexture();
    UpdateUniformBuffer();
}

// 每帧开始、绑定帧缓冲前准备 HDR / MSAA 目标。
void GLRender3D::OnPrepareFrame() {
    if (m_msaaEnabled) {
        EnsureMsaaTargets();
        m_msaaPassActive = (m_msaaTargetsReady && m_msaaFbo != 0);
    } else {
        m_msaaPassActive = false;
    }
    if (m_hdrEnabled) {
        EnsureHdrTarget();
    }
}

// 每帧结束时的钩子。
void GLRender3D::OnEndFrame() {
    // MSAA 时先把多重采样深度解析为单采样深度纹理，回读才能取到有效深度。
    if (m_msaaPassActive) {
        ResolveMsaaDepth();
    }
    ProcessDepthReadback();
}

// 交换链/帧缓冲重建后的钩子。
void GLRender3D::OnRecreateSwapchain() {
    DestroyDepthResources();
    CreateDepthResources();
    // 尺寸变化后释放 HDR / MSAA 中间目标，下一帧按新尺寸重建。
    DestroyHdrResources();
    DestroyMsaaResources();
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
        const std::string vertShaderPath = AssetPath::ShaderFile("3d.vert");
        const std::string fragShaderPath = AssetPath::ShaderFile("3d.frag");
        const std::vector<char> vertCode = ReadShaderFile(vertShaderPath);
        const std::vector<char> fragCode = ReadShaderFile(fragShaderPath);
        std::string vertSource(vertCode.begin(), vertCode.end());
        std::string fragSource(fragCode.begin(), fragCode.end());
        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSource.c_str());
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
        if (vert == 0 || frag == 0) {
            if (vert) m_functions->glDeleteShader(vert);
            if (frag) m_functions->glDeleteShader(frag);
            SetLastError("OpenGL 着色器编译失败: " + vertShaderPath + " 或 " + fragShaderPath);
            return false;
        }
        m_program = LinkProgram(vert, frag);
        m_functions->glDeleteShader(vert);
        m_functions->glDeleteShader(frag);
        if (m_program == 0) {
            SetLastError("OpenGL 程序链接失败: " + vertShaderPath + " / " + fragShaderPath);
            return false;
        }

        const std::string shadowVertShaderPath = AssetPath::ShaderFile("3d_shadow.vert");
        const std::string shadowFragShaderPath = AssetPath::ShaderFile("3d_shadow.frag");
        const std::vector<char> shadowVertCode = ReadShaderFile(shadowVertShaderPath);
        const std::vector<char> shadowFragCode = ReadShaderFile(shadowFragShaderPath);
        std::string shadowVertSource(shadowVertCode.begin(), shadowVertCode.end());
        std::string shadowFragSource(shadowFragCode.begin(), shadowFragCode.end());
        unsigned int shadowVert = CompileShader(GL_VERTEX_SHADER, shadowVertSource.c_str());
        unsigned int shadowFrag = CompileShader(GL_FRAGMENT_SHADER, shadowFragSource.c_str());
        if (shadowVert == 0 || shadowFrag == 0) {
            if (shadowVert) m_functions->glDeleteShader(shadowVert);
            if (shadowFrag) m_functions->glDeleteShader(shadowFrag);
            SetLastError("OpenGL 着色器编译失败: " + shadowVertShaderPath + " 或 " + shadowFragShaderPath);
            return false;
        }
        m_shadowProgram = LinkProgram(shadowVert, shadowFrag);
        m_functions->glDeleteShader(shadowVert);
        m_functions->glDeleteShader(shadowFrag);
        if (m_shadowProgram == 0) {
            SetLastError("OpenGL 程序链接失败: " + shadowVertShaderPath + " / " + shadowFragShaderPath);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SetLastError(e.what());
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
    // 材质 uniform 位置（1.4 / 1.5）：baseColor / alphaCutoff / alphaMode + PBR 标量 + 5 张纹理。
    m_uBaseColorLoc = m_functions->glGetUniformLocation(m_program, "uMaterialBaseColor");
    m_uAlphaCutoffLoc = m_functions->glGetUniformLocation(m_program, "uAlphaCutoff");
    m_uAlphaModeLoc = m_functions->glGetUniformLocation(m_program, "uAlphaMode");
    m_uMetallicLoc = m_functions->glGetUniformLocation(m_program, "uMetallic");
    m_uRoughnessLoc = m_functions->glGetUniformLocation(m_program, "uRoughness");
    m_uEmissiveFactorLoc = m_functions->glGetUniformLocation(m_program, "uEmissiveFactor");
    m_uEmissiveStrengthLoc = m_functions->glGetUniformLocation(m_program, "uEmissiveStrength");
    m_uNormalScaleLoc = m_functions->glGetUniformLocation(m_program, "uNormalScale");
    m_uOcclusionStrengthLoc = m_functions->glGetUniformLocation(m_program, "uOcclusionStrength");
    m_uBaseColorTextureLoc = m_functions->glGetUniformLocation(m_program, "baseColorTexture");
    m_uMetallicRoughnessTextureLoc =
        m_functions->glGetUniformLocation(m_program, "metallicRoughnessTexture");
    m_uNormalTextureLoc = m_functions->glGetUniformLocation(m_program, "normalTexture");
    m_uOcclusionTextureLoc = m_functions->glGetUniformLocation(m_program, "occlusionTexture");
    m_uEmissiveTextureLoc = m_functions->glGetUniformLocation(m_program, "emissiveTexture");
    // 逐对象世界矩阵 uniform（任务 2.1）：主程序与阴影程序各自的位置。
    m_uObjectModelLoc = m_functions->glGetUniformLocation(m_program, "uObjectModel");
    m_uShadowObjectModelLoc = m_functions->glGetUniformLocation(m_shadowProgram, "uObjectModel");
    // 选中高亮 uniform（任务 2.3）。
    m_uHighlightLoc = m_functions->glGetUniformLocation(m_program, "uHighlight");
    m_functions->glUseProgram(m_program);
    if (m_uBaseColorTextureLoc >= 0) {
        m_functions->glUniform1i(m_uBaseColorTextureLoc, kMaterialTextureUnit);
    }
    if (m_uMetallicRoughnessTextureLoc >= 0) {
        m_functions->glUniform1i(m_uMetallicRoughnessTextureLoc, kMetallicRoughnessTextureUnit);
    }
    if (m_uNormalTextureLoc >= 0) {
        m_functions->glUniform1i(m_uNormalTextureLoc, kNormalTextureUnit);
    }
    if (m_uOcclusionTextureLoc >= 0) {
        m_functions->glUniform1i(m_uOcclusionTextureLoc, kOcclusionTextureUnit);
    }
    if (m_uEmissiveTextureLoc >= 0) {
        m_functions->glUniform1i(m_uEmissiveTextureLoc, kEmissiveTextureUnit);
    }
    m_functions->glUseProgram(0);
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
    // 场景基础朝向（任务 2.1）：归一化场景空间为 Z-up（+Z 上），而相机基向量仍为
    // view up=+Y。用固定旋转 R（绕 X 轴 -90°）把场景 +Z 映射到显示 +Y，使模型竖直显示。
    // R 紧贴平移中心之后（轨道旋转之内侧），故轨道旋转/平移/约束/回读数学与旧版完全一致。
    const glm::mat4 sceneBase = glm::rotate(
        glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), m_panOffset)
        * glm::mat4_cast(m_modelRotation)
        * sceneBase
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

    m_invViewProj = glm::inverse(proj * view);
    m_renderToSource = m_normalizedToWorld * glm::inverse(model);
    // 任务 2.4：缓存本帧矩阵供 Gizmo 射线拾取与固定屏幕尺寸换算使用。
    m_lastInvViewProj = m_invViewProj;
    m_lastSceneFromRender = glm::inverse(model);
    m_lastModelView = view * model;
    m_lastProj = proj;

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
    // 阴影贴图与采样矩阵必须成对更新：只在阴影通道中重算并锁定光源矩阵，
    // 主绘制复用同一矩阵。否则贴图每 100ms 才刷新，而矩阵每帧随太阳变化，
    // 光照变化期间会出现阴影游动/抽搐。
    if (m_shadowPassActive) {
        m_shadowLightViewProj = ComputeLightViewProj();
        m_shadowMatrixValid = true;
    }
    m_lightViewProj = m_shadowLightViewProj;
    memcpy(ubo.lightViewProj, glm::value_ptr(m_lightViewProj), sizeof(float) * 16);
    ubo.shadowOptions[0] = ShouldRenderShadows() ? 1.0f : 0.0f;
    ubo.shadowOptions[1] = 1.0f;
    ubo.shadowOptions[2] = m_wireframeMode ? 1.0f : 0.0f;
    // OpenGL 窗口 Y 向上：dFdy 无需取反（与着色器默认一致）。
    ubo.shadowOptions[3] = 0.0f;
    // PBR 视线向量：把相机（显示世界空间）变换到“归一化场景空间”（着色空间，
    // 即 ubo.model 之前），与片元的 fragObjectPosition、物体空间太阳方向一致。
    const glm::vec3 cameraObject = glm::vec3(glm::inverse(model) * glm::vec4(eye, 1.0f));
    ubo.cameraWorldPosition[0] = cameraObject.x;
    ubo.cameraWorldPosition[1] = cameraObject.y;
    ubo.cameraWorldPosition[2] = cameraObject.z;
    ubo.cameraWorldPosition[3] = 0.0f;
    // 1.7：半球环境光（线性）。
    ubo.ambientSkyColor[0] = m_ambientSkyColor.x;
    ubo.ambientSkyColor[1] = m_ambientSkyColor.y;
    ubo.ambientSkyColor[2] = m_ambientSkyColor.z;
    ubo.ambientSkyColor[3] = m_ambientIntensity;
    ubo.ambientGroundColor[0] = m_ambientGroundColor.x;
    ubo.ambientGroundColor[1] = m_ambientGroundColor.y;
    ubo.ambientGroundColor[2] = m_ambientGroundColor.z;
    ubo.ambientGroundColor[3] = 0.0f;
    // 1.7：阴影偏移/PCF/法线偏移。
    ubo.shadowParams[0] = m_shadowBias;
    ubo.shadowParams[1] = static_cast<float>(m_shadowPcfMode);
    ubo.shadowParams[2] = (m_shadowNormalOffsetScale > 0.0f && m_allocatedShadowTextureSize > 0)
        ? (2.0f * glm::length(glm::vec3(m_shadowBoundsMax - m_shadowBoundsMin))
            / static_cast<float>(m_allocatedShadowTextureSize)) * m_shadowNormalOffsetScale
        : 0.0f;
    ubo.shadowParams[3] = 0.0f;
    // 1.7：调试视图 + 线性深度归一化范围。
    ubo.debugOptions[0] = static_cast<float>(m_debugView);
    ubo.debugOptions[1] = 0.0f;
    ubo.debugOptions[2] = 0.0f;
    ubo.debugOptions[3] = 0.0f;
    ubo.depthRange[0] = 0.1f;
    ubo.depthRange[1] = 100.0f;
    ubo.depthRange[2] = 0.0f;
    ubo.depthRange[3] = 0.0f;

    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    m_functions->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
    m_functions->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// 以材质参数（普通 uniform）+ 纹理单元绑定后绘制当前网格。
void GLRender3D::ApplyMaterial(const MaterialParams& params, const MaterialTextureSet& textures) {
    if (!m_functions || m_program == 0) return;
    m_functions->glUseProgram(m_program);
    m_currentProgram = m_program;
    m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);

    // PBR 调试覆盖：按 UI 开关替换对应参数后再上传 uniform。
    const float metallic = m_metallicOverrideEnabled ? m_metallicOverride : params.metallic;
    const float roughness = m_roughnessOverrideEnabled ? m_roughnessOverride : params.roughness;
    const float emissiveStrength =
        m_emissiveOverrideEnabled ? m_emissiveOverride : params.emissiveStrength;

    if (m_uBaseColorLoc >= 0) {
        m_functions->glUniform4f(m_uBaseColorLoc, params.baseColor[0], params.baseColor[1],
                                 params.baseColor[2], params.baseColor[3]);
    }
    if (m_uAlphaCutoffLoc >= 0) {
        m_functions->glUniform1f(m_uAlphaCutoffLoc, params.alphaCutoff);
    }
    if (m_uAlphaModeLoc >= 0) {
        m_functions->glUniform1i(m_uAlphaModeLoc, params.alphaMode);
    }
    if (m_uMetallicLoc >= 0) {
        m_functions->glUniform1f(m_uMetallicLoc, metallic);
    }
    if (m_uRoughnessLoc >= 0) {
        m_functions->glUniform1f(m_uRoughnessLoc, roughness);
    }
    if (m_uEmissiveFactorLoc >= 0) {
        m_functions->glUniform4f(m_uEmissiveFactorLoc, params.emissiveFactor[0],
                                 params.emissiveFactor[1], params.emissiveFactor[2],
                                 params.emissiveFactor[3]);
    }
    if (m_uEmissiveStrengthLoc >= 0) {
        m_functions->glUniform1f(m_uEmissiveStrengthLoc, emissiveStrength);
    }
    if (m_uNormalScaleLoc >= 0) {
        m_functions->glUniform1f(m_uNormalScaleLoc, params.normalScale);
    }
    if (m_uOcclusionStrengthLoc >= 0) {
        m_functions->glUniform1f(m_uOcclusionStrengthLoc, params.occlusionStrength);
    }
    if (m_uHighlightLoc >= 0) {
        m_functions->glUniform1i(m_uHighlightLoc, m_currentHighlight ? 1 : 0);
    }

    // 确保默认贴图存在（白 / 平面法线）。
    auto ensureDefaults = [this]() {
        if (m_defaultWhiteHandle == 0 || !IsTextureValid(m_defaultWhiteHandle)) {
            const uint8_t white[4] = { 255, 255, 255, 255 };
            TextureDesc desc;
            desc.width = 1;
            desc.height = 1;
            desc.semantic = TextureSemantic::Color;
            desc.generateMipmaps = false;
            desc.pixels = white;
            desc.sizeBytes = sizeof(white);
            desc.debugName = "default-white";
            desc.cacheKey = "builtin:default-white";
            m_defaultWhiteHandle = CreateTexture(desc);
        }
        if (m_defaultFlatNormalHandle == 0 || !IsTextureValid(m_defaultFlatNormalHandle)) {
            const uint8_t flatNormal[4] = { 128, 128, 255, 255 };
            TextureDesc desc;
            desc.width = 1;
            desc.height = 1;
            desc.semantic = TextureSemantic::Normal;
            desc.generateMipmaps = false;
            desc.pixels = flatNormal;
            desc.sizeBytes = sizeof(flatNormal);
            desc.debugName = "default-flat-normal";
            desc.cacheKey = "builtin:default-flat-normal";
            m_defaultFlatNormalHandle = CreateTexture(desc);
        }
    };
    ensureDefaults();
    const unsigned int whiteTexture = (m_defaultWhiteHandle != 0 && IsTextureValid(m_defaultWhiteHandle))
        ? GetTextureObject(m_defaultWhiteHandle) : 0;
    const unsigned int flatNormalTexture = (m_defaultFlatNormalHandle != 0 && IsTextureValid(m_defaultFlatNormalHandle))
        ? GetTextureObject(m_defaultFlatNormalHandle) : 0;

    auto bindTexture = [this](TextureHandle handle, unsigned int fallback,
                              int unit) {
        unsigned int glTexture = fallback;
        if (handle != 0 && IsTextureValid(handle)) {
            const unsigned int object = GetTextureObject(handle);
            if (object != 0) glTexture = object;
        }
        m_functions->glActiveTexture(GL_TEXTURE0 + unit);
        m_functions->glBindTexture(GL_TEXTURE_2D, glTexture);
        m_functions->glActiveTexture(GL_TEXTURE0);
    };

    bindTexture(textures.baseColor, whiteTexture, kMaterialTextureUnit);
    bindTexture(textures.metallicRoughness, whiteTexture, kMetallicRoughnessTextureUnit);
    bindTexture(textures.normal, flatNormalTexture, kNormalTextureUnit);
    bindTexture(textures.occlusion, whiteTexture, kOcclusionTextureUnit);
    bindTexture(textures.emissive, whiteTexture, kEmissiveTextureUnit);
}

// 设置当前对象的逐对象世界矩阵（任务 2.1）：按当前激活的程序选择 uniform 位置。
void GLRender3D::SetObjectModelMatrix(const float model[16]) {
    if (!m_functions || !model) return;
    // 以“当前绑定的程序”为准选择 uniform 位置，避免依赖成员状态。
    GLint boundProgram = 0;
    m_functions->glGetIntegerv(GL_CURRENT_PROGRAM, &boundProgram);
    const int location = (static_cast<unsigned int>(boundProgram) == m_shadowProgram &&
                          m_shadowProgram != 0)
        ? m_uShadowObjectModelLoc
        : m_uObjectModelLoc;
    if (location >= 0) {
        // 矩阵为列主序，与 GLSL 一致，transpose = GL_FALSE。
        m_functions->glUniformMatrix4fv(location, 1, GL_FALSE, model);
    }
}

// 计算点（上传坐标空间）到相机的距离，用于透明排序。
float GLRender3D::ComputeDrawDistance(const float center[3]) const {
    const glm::vec3 eye(0.0f, 0.0f, m_orbitDistance);
    // 与 UpdateUniformBuffer 的显示变换保持一致（含场景基础朝向）。
    const glm::mat4 sceneBase = glm::rotate(
        glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), m_panOffset)
        * sceneBase
        * glm::mat4_cast(m_modelRotation)
        * glm::translate(glm::mat4(1.0f), -m_orbitCenter);
    const glm::vec3 world = glm::vec3(model * glm::vec4(center[0], center[1], center[2], 1.0f));
    return glm::length(eye - world);
}

// 按距离从远到近排序并绘制已收集的透明请求。
void GLRender3D::FlushTransparentDraws() {
    if (m_transparentDraws.empty()) return;

    std::stable_sort(m_transparentDraws.begin(), m_transparentDraws.end(),
        [](const TransparentDraw& a, const TransparentDraw& b) {
            return a.distance > b.distance;  // 远的先画
        });

    if (m_functions) {
        m_functions->glEnable(GL_BLEND);
        m_functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_functions->glDepthMask(GL_FALSE);  // 透明不写深度
    }
    for (const TransparentDraw& draw : m_transparentDraws) {
        if (draw.object) {
            draw.object->DrawSubMesh(draw.subMeshIndex, Object::RM_TRANSPARENT);
        }
    }
    if (m_functions) {
        m_functions->glDepthMask(GL_TRUE);
        m_functions->glDisable(GL_BLEND);
    }
    m_transparentDraws.clear();
}
// 创建深度附件。
bool GLRender3D::CreateDepthResources() { return true; }
// 销毁深度附件。
void GLRender3D::DestroyDepthResources() {}

// ---------------- HDR + 色调映射（1.6） ----------------

// 开启/关闭 HDR 路径。关闭时完全回退到默认帧缓冲直出。
void GLRender3D::SetHdrEnabled(bool enabled) {
    m_hdrEnabled = enabled;
}

// 设置曝光（EV 档位）。
void GLRender3D::SetExposureEV(float ev) {
    m_exposureEV = ev;
}

// 1.7：阴影基础深度偏移。
void GLRender3D::SetShadowBias(float bias) {
    m_shadowBias = bias;
}

// 1.7：PCF 档位（0 关闭 / 1 = 3x3 / 2 = 5x5）。
void GLRender3D::SetShadowPcfMode(int mode) {
    m_shadowPcfMode = (mode < 0) ? 0 : (mode > 2 ? 2 : mode);
}

// 1.7：法线偏移的世界纹素倍数。
void GLRender3D::SetShadowNormalOffsetScale(float scale) {
    m_shadowNormalOffsetScale = (scale < 0.0f) ? 0.0f : scale;
}

// 1.7：半球环境光（线性空间）。
void GLRender3D::SetAmbientLight(const Vec3& skyColor, const Vec3& groundColor, float intensity) {
    m_ambientSkyColor = glm::vec3(skyColor.x, skyColor.y, skyColor.z);
    m_ambientGroundColor = glm::vec3(groundColor.x, groundColor.y, groundColor.z);
    m_ambientIntensity = (intensity < 0.0f) ? 0.0f : intensity;
}

// 1.7：调试视图（0 正常 / 1 深度 / 2 世界法线 / 3 阴影）。
void GLRender3D::SetDebugView(int view) {
    m_debugView = (view < 0) ? 0 : (view > 3 ? 3 : view);
}

// 1.7：开关 4x MSAA。
void GLRender3D::SetMsaaEnabled(bool enabled) {
    m_msaaEnabled = enabled;
}

// 本帧场景绘制的目标帧缓冲：
//   4x MSAA 开启且目标就绪 -> 多重采样 FBO；
//   否则 HDR 开启且就绪 -> HDR FBO；
//   否则默认帧缓冲。
unsigned int GLRender3D::GetSceneFramebuffer() const {
    if (m_msaaEnabled && m_msaaTargetsReady && m_msaaFbo != 0) {
        return m_msaaFbo;
    }
    if (m_hdrEnabled && m_hdrFbo != 0 && m_postProgram != 0) {
        return m_hdrFbo;
    }
    return 0;
}

// 场景绘制完成后：MSAA 时先把多重采样颜色/深度解析到单采样目标，再按 HDR 开关后处理或直出。
void GLRender3D::OnAfterSceneRender() {
    if (m_msaaPassActive) {
        // 颜色解析：多重采样 -> 单采样 RGBA16F 纹理。
        if (m_functions && m_msaaResolveFbo != 0) {
            m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
            m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_msaaResolveFbo);
            m_functions->glBlitFramebuffer(0, 0,
                static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
                0, 0, static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
                GL_COLOR_BUFFER_BIT, GL_NEAREST);
            m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }
        // 深度解析（供世界坐标回读）。
        ResolveMsaaDepth();

        if (m_hdrEnabled) {
            // HDR：后处理读取 MSAA 解析结果。
            RenderPostPass();
        } else {
            // LDR：直接把解析结果拷贝到默认帧缓冲（不做 gamma，与旧 LDR 路径一致）。
            if (m_functions && m_msaaResolveFbo != 0) {
                m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaResolveFbo);
                m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                m_functions->glBlitFramebuffer(0, 0,
                    static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
                    0, 0, static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
                m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            }
        }
        return;
    }

    if (!m_hdrEnabled) return;
    RenderPostPass();
}

// 确保 HDR 中间目标与当前尺寸匹配；尺寸变化或未创建时重建。
bool GLRender3D::EnsureHdrTarget() {
    if (!m_functions || m_postProgram == 0) return false;
    const int width = static_cast<int>(m_framebufferWidth);
    const int height = static_cast<int>(m_framebufferHeight);
    if (width <= 0 || height <= 0) return false;

    if (m_hdrFbo != 0 && m_hdrWidth == width && m_hdrHeight == height) {
        return true;
    }

    DestroyHdrResources();

    // 颜色附件：线性 RGBA16F（HDR 计算需要 >1 的精度与范围）。
    m_functions->glGenTextures(1, &m_hdrColorTexture);
    m_functions->glBindTexture(GL_TEXTURE_2D, m_hdrColorTexture);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
        GL_RGBA, GL_FLOAT, nullptr);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_functions->glBindTexture(GL_TEXTURE_2D, 0);

    // 深度渲染缓冲。
    m_functions->glGenRenderbuffers(1, &m_hdrDepthRbo);
    m_functions->glBindRenderbuffer(GL_RENDERBUFFER, m_hdrDepthRbo);
    m_functions->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    m_functions->glBindRenderbuffer(GL_RENDERBUFFER, 0);

    m_functions->glGenFramebuffers(1, &m_hdrFbo);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);
    m_functions->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, m_hdrColorTexture, 0);
    m_functions->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, m_hdrDepthRbo);

    const unsigned int status = m_functions->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SetLastError("OpenGL: HDR 中间目标帧缓冲不完整");
        DestroyHdrResources();
        return false;
    }

    m_hdrWidth = width;
    m_hdrHeight = height;
    return true;
}

// 销毁 HDR 中间目标（FBO + 颜色纹理 + 深度渲染缓冲）。
void GLRender3D::DestroyHdrResources() {
    if (!m_functions) {
        m_hdrFbo = 0;
        m_hdrColorTexture = 0;
        m_hdrDepthRbo = 0;
        m_hdrWidth = 0;
        m_hdrHeight = 0;
        return;
    }
    if (m_hdrFbo != 0) {
        m_functions->glDeleteFramebuffers(1, &m_hdrFbo);
        m_hdrFbo = 0;
    }
    if (m_hdrDepthRbo != 0) {
        m_functions->glDeleteRenderbuffers(1, &m_hdrDepthRbo);
        m_hdrDepthRbo = 0;
    }
    if (m_hdrColorTexture != 0) {
        m_functions->glDeleteTextures(1, &m_hdrColorTexture);
        m_hdrColorTexture = 0;
    }
    m_hdrWidth = 0;
    m_hdrHeight = 0;
}

// 创建 HDR 后处理程序（全屏三角形 + 曝光 + ACES）。
bool GLRender3D::CreatePostProgram() {
    if (!m_functions) return false;
    if (m_postProgram != 0) return true;
    try {
        const std::string vertPath = AssetPath::ShaderFile("post.vert");
        const std::string fragPath = AssetPath::ShaderFile("post.frag");
        const std::vector<char> vertCode = ReadShaderFile(vertPath);
        const std::vector<char> fragCode = ReadShaderFile(fragPath);
        std::string vertSource(vertCode.begin(), vertCode.end());
        std::string fragSource(fragCode.begin(), fragCode.end());
        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSource.c_str());
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
        if (vert == 0 || frag == 0) {
            if (vert) m_functions->glDeleteShader(vert);
            if (frag) m_functions->glDeleteShader(frag);
            SetLastError("OpenGL 后处理着色器编译失败: " + vertPath + " 或 " + fragPath);
            return false;
        }
        m_postProgram = LinkProgram(vert, frag);
        m_functions->glDeleteShader(vert);
        m_functions->glDeleteShader(frag);
        if (m_postProgram == 0) {
            SetLastError("OpenGL 后处理程序链接失败: " + vertPath + " / " + fragPath);
            return false;
        }

        m_uPostExposureLoc = m_functions->glGetUniformLocation(m_postProgram, "uPostParams");
        m_uPostTextureLoc = m_functions->glGetUniformLocation(m_postProgram, "hdrTexture");
        m_functions->glUseProgram(m_postProgram);
        if (m_uPostTextureLoc >= 0) {
            m_functions->glUniform1i(m_uPostTextureLoc, 0);
        }
        m_functions->glUseProgram(0);

        // Core Profile 下绘制必须绑定一个 VAO；全屏三角形不需要顶点数据，用空 VAO 即可。
        m_functions->glGenVertexArrays(1, &m_postVao);
        return true;
    } catch (const std::exception& e) {
        SetLastError(e.what());
        std::cerr << "OpenGL 读取后处理着色器失败: " << e.what() << std::endl;
        return false;
    }
}

// 销毁 HDR 后处理程序。
void GLRender3D::DestroyPostProgram() {
    if (m_functions && m_postProgram != 0) {
        m_functions->glDeleteProgram(m_postProgram);
    }
    if (m_functions && m_postVao != 0) {
        m_functions->glDeleteVertexArrays(1, &m_postVao);
    }
    m_postProgram = 0;
    m_postVao = 0;
    m_uPostExposureLoc = -1;
    m_uPostTextureLoc = -1;
}

// 执行后处理：输入纹理 -> 曝光 + ACES（或调试直出）-> 默认帧缓冲。
// 输入纹理：MSAA 开启时用多重采样解析结果，否则用 HDR 中间目标颜色。
void GLRender3D::RenderPostPass() {
    if (!m_functions || m_postProgram == 0) return;

    unsigned int inputTexture = m_hdrColorTexture;
    if (m_msaaPassActive && m_msaaResolveColorTexture != 0) {
        inputTexture = m_msaaResolveColorTexture;
    } else if (!EnsureHdrTarget()) {
        return;
    }
    if (inputTexture == 0) return;

    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_functions->glViewport(0, 0,
        static_cast<int>(m_framebufferWidth),
        static_cast<int>(m_framebufferHeight));
    m_functions->glDisable(GL_DEPTH_TEST);
    m_functions->glDisable(GL_BLEND);

    m_functions->glUseProgram(m_postProgram);
    m_currentProgram = m_postProgram;

    // x: 曝光乘数 = 2^EV；y: 调试标志（调试视图跳过曝光与色调映射）。
    if (m_uPostExposureLoc >= 0) {
        const float exposure = std::pow(2.0f, m_exposureEV);
        const float debugFlag = (m_debugView != 0) ? 1.0f : 0.0f;
        m_functions->glUniform4f(m_uPostExposureLoc, exposure, debugFlag, 0.0f, 0.0f);
    }

    m_functions->glActiveTexture(GL_TEXTURE0);
    m_functions->glBindTexture(GL_TEXTURE_2D, inputTexture);

    m_functions->glBindVertexArray(m_postVao);
    m_functions->glDrawArrays(GL_TRIANGLES, 0, 3);
    m_functions->glBindVertexArray(0);

    m_functions->glActiveTexture(GL_TEXTURE0);
    m_functions->glBindTexture(GL_TEXTURE_2D, 0);
    m_functions->glEnable(GL_DEPTH_TEST);
}

// ---------------- 1.7 4x MSAA + 深度解析 ----------------

// 确保 4x MSAA 目标与当前尺寸/模式匹配；必要时创建。
bool GLRender3D::EnsureMsaaTargets() {
    if (!m_functions) return false;
    const int width = static_cast<int>(m_framebufferWidth);
    const int height = static_cast<int>(m_framebufferHeight);
    if (width <= 0 || height <= 0) return false;

    if (m_msaaTargetsReady && m_msaaWidth == width && m_msaaHeight == height) {
        return true;
    }
    return CreateMsaaResources();
}

// 创建多重采样颜色/深度渲染缓冲、解析目标（单采样颜色纹理 + 深度纹理）。
bool GLRender3D::CreateMsaaResources() {
    if (!m_functions) return false;
    const int width = static_cast<int>(m_framebufferWidth);
    const int height = static_cast<int>(m_framebufferHeight);
    if (width <= 0 || height <= 0) return false;

    DestroyMsaaResources();

    int maxSamples = 0;
    m_functions->glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_msaaSamples = (maxSamples >= 4) ? 4 : (maxSamples >= 2 ? 2 : 0);
    if (m_msaaSamples == 0) {
        SetLastError("OpenGL: 设备不支持多重采样，MSAA 不可用");
        return false;
    }

    // 多重采样颜色渲染缓冲（RGBA16F 线性，供 HDR 后处理）。
    m_functions->glGenRenderbuffers(1, &m_msaaColorRbo);
    m_functions->glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
    m_functions->glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_RGBA16F, width, height);

    // 多重采样深度渲染缓冲。
    m_functions->glGenRenderbuffers(1, &m_msaaDepthRbo);
    m_functions->glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRbo);
    m_functions->glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_DEPTH_COMPONENT24, width, height);
    m_functions->glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // 多重采样帧缓冲。
    m_functions->glGenFramebuffers(1, &m_msaaFbo);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    m_functions->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER, m_msaaColorRbo);
    m_functions->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, m_msaaDepthRbo);
    unsigned int status = m_functions->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SetLastError("OpenGL: MSAA 帧缓冲不完整");
        DestroyMsaaResources();
        return false;
    }

    // 解析目标：单采样颜色纹理（RGBA16F）+ 深度纹理。
    m_functions->glGenTextures(1, &m_msaaResolveColorTexture);
    m_functions->glBindTexture(GL_TEXTURE_2D, m_msaaResolveColorTexture);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_functions->glGenTextures(1, &m_msaaResolveDepthTexture);
    m_functions->glBindTexture(GL_TEXTURE_2D, m_msaaResolveDepthTexture);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_functions->glBindTexture(GL_TEXTURE_2D, 0);

    m_functions->glGenFramebuffers(1, &m_msaaResolveFbo);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, m_msaaResolveFbo);
    m_functions->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, m_msaaResolveColorTexture, 0);
    m_functions->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, m_msaaResolveDepthTexture, 0);
    status = m_functions->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SetLastError("OpenGL: MSAA 解析帧缓冲不完整");
        DestroyMsaaResources();
        return false;
    }

    m_msaaWidth = width;
    m_msaaHeight = height;
    m_msaaTargetsReady = true;
    return true;
}

// 销毁尺寸相关的 MSAA 资源。
void GLRender3D::DestroyMsaaResources() {
    if (!m_functions) {
        m_msaaFbo = 0;
        m_msaaColorRbo = 0;
        m_msaaDepthRbo = 0;
        m_msaaResolveFbo = 0;
        m_msaaResolveColorTexture = 0;
        m_msaaResolveDepthTexture = 0;
        m_msaaWidth = 0;
        m_msaaHeight = 0;
        m_msaaTargetsReady = false;
        return;
    }
    if (m_msaaFbo != 0) {
        m_functions->glDeleteFramebuffers(1, &m_msaaFbo);
        m_msaaFbo = 0;
    }
    if (m_msaaResolveFbo != 0) {
        m_functions->glDeleteFramebuffers(1, &m_msaaResolveFbo);
        m_msaaResolveFbo = 0;
    }
    if (m_msaaColorRbo != 0) {
        m_functions->glDeleteRenderbuffers(1, &m_msaaColorRbo);
        m_msaaColorRbo = 0;
    }
    if (m_msaaDepthRbo != 0) {
        m_functions->glDeleteRenderbuffers(1, &m_msaaDepthRbo);
        m_msaaDepthRbo = 0;
    }
    if (m_msaaResolveColorTexture != 0) {
        m_functions->glDeleteTextures(1, &m_msaaResolveColorTexture);
        m_msaaResolveColorTexture = 0;
    }
    if (m_msaaResolveDepthTexture != 0) {
        m_functions->glDeleteTextures(1, &m_msaaResolveDepthTexture);
        m_msaaResolveDepthTexture = 0;
    }
    m_msaaWidth = 0;
    m_msaaHeight = 0;
    m_msaaTargetsReady = false;
}

// 把多重采样深度解析为单采样深度纹理（glBlitFramebuffer），供世界坐标回读。
void GLRender3D::ResolveMsaaDepth() {
    if (!m_functions || m_msaaFbo == 0 || m_msaaResolveFbo == 0) return;
    m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_msaaResolveFbo);
    m_functions->glBlitFramebuffer(0, 0,
        static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
        0, 0, static_cast<int>(m_framebufferWidth), static_cast<int>(m_framebufferHeight),
        GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    m_functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    m_functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}
// 创建深度回读缓冲。
bool GLRender3D::CreateDepthReadbackResources() { return true; }
// 销毁深度回读缓冲。
void GLRender3D::DestroyDepthReadbackResources() {}

// 把回读深度反投影为世界坐标。
void GLRender3D::ProcessDepthReadback() {
    if (!m_depthReadbackRequested || !m_functions) return;
    m_depthReadbackRequested = false;

    if (m_framebufferWidth == 0 || m_framebufferHeight == 0) return;

    // 深度可能写在 HDR / MSAA 中间目标里，回读时绑定当前场景帧缓冲。
    // MSAA 时深度已解析到 m_msaaResolveFbo 的单采样深度纹理，回读它即可。
    unsigned int readFbo = GetSceneFramebuffer();
    if (m_msaaPassActive && m_msaaResolveFbo != 0) {
        readFbo = m_msaaResolveFbo;
    }
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, readFbo);

    int32_t pixelX = static_cast<int32_t>(m_requestedNDCX *
        static_cast<float>(m_framebufferWidth));
    int32_t pixelY = static_cast<int32_t>((1.0f - m_requestedNDCY) *
        static_cast<float>(m_framebufferHeight));
    pixelX = std::max(0, std::min(pixelX,
        static_cast<int32_t>(m_framebufferWidth) - 1));
    pixelY = std::max(0, std::min(pixelY,
        static_cast<int32_t>(m_framebufferHeight) - 1));

    float depth = 1.0f;
    m_functions->glReadPixels(pixelX, pixelY, 1, 1,
        GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

    const float x_ndc = m_requestedNDCX * 2.0f - 1.0f;
    const float y_ndc = 1.0f - m_requestedNDCY * 2.0f;
    const float z_ndc = depth * 2.0f - 1.0f;

    glm::vec4 worldPos{};
    if (std::isfinite(depth) && depth >= 0.0f && depth < 0.999f) {
        const glm::vec4 clipPos(x_ndc, y_ndc, z_ndc, 1.0f);
        glm::vec4 renderPos = m_invViewProj * clipPos;
        renderPos /= renderPos.w;
        worldPos = m_renderToSource * renderPos;
        worldPos /= worldPos.w;
    } else {
        glm::vec4 nearRender = m_invViewProj * glm::vec4(x_ndc, y_ndc, -1.0f, 1.0f);
        glm::vec4 farRender  = m_invViewProj * glm::vec4(x_ndc, y_ndc,  1.0f, 1.0f);
        nearRender /= nearRender.w;
        farRender  /= farRender.w;

        glm::vec4 nearWorld = m_renderToSource * nearRender;
        glm::vec4 farWorld = m_renderToSource * farRender;
        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;

        const glm::vec3 rayOrigin(nearWorld);
        const glm::vec3 rayDirection = glm::normalize(glm::vec3(farWorld - nearWorld));
        const float denominator = rayDirection.z;
        const float distance = std::abs(denominator) > 1.0e-6f
            ? -rayOrigin.z / denominator
            : 0.0f;
        worldPos = glm::vec4(rayOrigin + rayDirection * distance, 1.0f);
    }

    m_lastWorldCoord[0] = worldPos.x;
    m_lastWorldCoord[1] = worldPos.y;
    m_lastWorldCoord[2] = worldPos.z;
    m_newCoordAvailable = true;
}

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

// PBR 调试覆盖（1.5）：覆盖材质的 metallic。
void GLRender3D::SetMetallicOverride(bool enabled, float value) {
    m_metallicOverrideEnabled = enabled;
    m_metallicOverride = value;
}

// PBR 调试覆盖（1.5）：覆盖材质的 roughness。
void GLRender3D::SetRoughnessOverride(bool enabled, float value) {
    m_roughnessOverrideEnabled = enabled;
    m_roughnessOverride = value;
}

// PBR 调试覆盖（1.5）：覆盖材质的自发光倍率。
void GLRender3D::SetEmissiveOverride(bool enabled, float value) {
    m_emissiveOverrideEnabled = enabled;
    m_emissiveOverride = value;
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
    return m_lightAnalysisEnabled && m_sunAboveHorizon && m_shadowMapReady
        && m_shadowMatrixValid && !m_shadowPassActive;
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

    // 选择与太阳最不平行的世界轴作为 up，避免太阳接近天顶时 lookAt 退化，
    // 造成每帧 roll 剧烈摆动（阴影抖动）。此选择保证 |dot(sun, up)| <= 1/sqrt(3)。
    const glm::vec3 worldAxes[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    glm::vec3 up = worldAxes[0];
    float minAlignment = std::abs(glm::dot(sun, worldAxes[0]));
    for (int axis = 1; axis < 3; ++axis) {
        const float alignment = std::abs(glm::dot(sun, worldAxes[axis]));
        if (alignment < minAlignment) {
            minAlignment = alignment;
            up = worldAxes[axis];
        }
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

    // 1.7：光源正交投影 texel 对齐（与 Vulkan 一致），消除阴影游动/闪烁。
    const uint32_t shadowSize = (m_allocatedShadowTextureSize != 0)
        ? m_allocatedShadowTextureSize : m_shadowTextureSize;
    const float halfSize = std::max(viewMax.x - viewMin.x, viewMax.y - viewMin.y) * 0.5f;
    const float texelWorld = (shadowSize != 0 && halfSize > 1.0e-6f)
        ? (2.0f * halfSize / static_cast<float>(shadowSize)) : 0.0f;
    float centerX = (viewMin.x + viewMax.x) * 0.5f;
    float centerY = (viewMin.y + viewMax.y) * 0.5f;
    if (texelWorld > 0.0f) {
        centerX = std::floor(centerX / texelWorld) * texelWorld;
        centerY = std::floor(centerY / texelWorld) * texelWorld;
    }

    return glm::orthoRH_NO(
        centerX - halfSize, centerX + halfSize,
        centerY - halfSize, centerY + halfSize,
        zNear, zFar) * lightView;
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
    m_shadowMatrixValid = false;
}

// 尝试分配指定尺寸的阴影贴图。
bool GLRender3D::TryAllocateShadowMap(uint32_t size) {
    DestroyShadowMap();
    if (!CreateShadowMap(size)) return false;
    m_shadowMapReady = true;
    m_allocatedShadowTextureSize = size;
    m_shadowMatrixValid = false;
    return true;
}

// 光照分析开启时保证阴影贴图可用。
bool GLRender3D::EnsureShadowMapForAnalysis() {
    if (!m_initialized || m_deviceLost || !m_lightAnalysisEnabled) return false;
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
    if (!m_initialized || m_deviceLost) return false;
    if (m_framebufferWidth == 0 || m_framebufferHeight == 0) return false;
    if (!m_lightAnalysisEnabled || !m_sunAboveHorizon) return false;
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

// 设置当前绘制对象的选中高亮标志（任务 2.3）。实际写入发生在 ApplyMaterial
// （随 uHighlight 一起上传）。
void GLRender3D::SetObjectHighlight(bool highlighted) {
    m_currentHighlight = highlighted;
}

// ---------------- 任务 2.4：Transform Gizmo 叠加层 ----------------

// 提交本帧要绘制的 Gizmo 线段顶点（归一化场景空间；每顶点 7 float：pos.xyz + color.rgba）。
void GLRender3D::SetGizmoGeometry(const float* interleavedPositionColor, uint32_t vertexCount) {
    if (!interleavedPositionColor || vertexCount == 0) {
        m_gizmoVertices.clear();
        m_gizmoVertexCount = 0;
        return;
    }
    const size_t floatCount = static_cast<size_t>(vertexCount) * 7;
    m_gizmoVertices.assign(interleavedPositionColor, interleavedPositionColor + floatCount);
    m_gizmoVertexCount = vertexCount;
}

// 屏幕归一化坐标（0..1，左上原点）-> 归一化场景空间射线。
bool GLRender3D::GetSceneRay(float nx, float ny, Vec3& origin, Vec3& direction) const {
    // OpenGL 窗口 Y 向上；像素 Y 已按 (1-ny) 处理，NDC 用标准 [-1,1]（深度 NO）。
    const float xNdc = nx * 2.0f - 1.0f;
    const float yNdc = 1.0f - ny * 2.0f;

    glm::vec4 nearRender = m_lastInvViewProj * glm::vec4(xNdc, yNdc, -1.0f, 1.0f);
    glm::vec4 farRender = m_lastInvViewProj * glm::vec4(xNdc, yNdc, 1.0f, 1.0f);
    if (std::fabs(nearRender.w) < 1.0e-8f || std::fabs(farRender.w) < 1.0e-8f) return false;
    nearRender /= nearRender.w;
    farRender /= farRender.w;

    glm::vec4 nearScene = m_lastSceneFromRender * nearRender;
    glm::vec4 farScene = m_lastSceneFromRender * farRender;
    if (std::fabs(nearScene.w) < 1.0e-8f || std::fabs(farScene.w) < 1.0e-8f) return false;
    nearScene /= nearScene.w;
    farScene /= farScene.w;

    const glm::vec3 dir = glm::vec3(farScene - nearScene);
    if (glm::length(dir) < 1.0e-8f) return false;
    origin = Vec3{ nearScene.x, nearScene.y, nearScene.z };
    const glm::vec3 unit = glm::normalize(dir);
    direction = Vec3{ unit.x, unit.y, unit.z };
    return true;
}

// 某归一化场景空间点处“每屏幕像素对应的世界长度”（固定屏幕尺寸 Gizmo 用）。
float GLRender3D::GetSceneWorldPerPixel(const Vec3& scenePoint) const {
    if (m_framebufferHeight == 0) return 0.0f;
    const float height = static_cast<float>(m_framebufferHeight);
    const float projYY = std::fabs(m_lastProj[1][1]);
    if (projYY <= 1.0e-8f) return 0.0f;

    float depth = 1.0f;
    if (!m_orthographicEnabled) {
        const glm::vec4 viewPos = m_lastModelView *
            glm::vec4(scenePoint.x, scenePoint.y, scenePoint.z, 1.0f);
        depth = std::fabs(viewPos.z);
        if (depth < 1.0e-4f) depth = 1.0e-4f;
    }
    return (2.0f * depth) / (projYY * height);
}

// 叠加层通道钩子：仅在存在 Gizmo 顶点时绘制。
void GLRender3D::OnOverlayPass() {
    if (m_gizmoVertexCount == 0 || m_gizmoVertices.empty()) return;
    DrawGizmoOverlay();
    m_gizmoVertices.clear();
    m_gizmoVertexCount = 0;
}

// 创建 Gizmo 线段程序（复用相机 UBO + 顶点色）。
bool GLRender3D::CreateGizmoProgram() {
    if (!m_functions) return false;
    if (m_gizmoProgram != 0) return true;
    try {
        const std::string vertPath = AssetPath::ShaderFile("gizmo.vert");
        const std::string fragPath = AssetPath::ShaderFile("gizmo.frag");
        const std::vector<char> vertCode = ReadShaderFile(vertPath);
        const std::vector<char> fragCode = ReadShaderFile(fragPath);
        std::string vertSource(vertCode.begin(), vertCode.end());
        std::string fragSource(fragCode.begin(), fragCode.end());
        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSource.c_str());
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
        if (vert == 0 || frag == 0) {
            if (vert) m_functions->glDeleteShader(vert);
            if (frag) m_functions->glDeleteShader(frag);
            SetLastError("OpenGL Gizmo 着色器编译失败: " + vertPath + " 或 " + fragPath);
            return false;
        }
        m_gizmoProgram = LinkProgram(vert, frag);
        m_functions->glDeleteShader(vert);
        m_functions->glDeleteShader(frag);
        if (m_gizmoProgram == 0) {
            SetLastError("OpenGL Gizmo 程序链接失败: " + vertPath + " / " + fragPath);
            return false;
        }

        // Gizmo 着色器用 ubo 的 model/view/proj（与 3d.frag 同一 UBO 块 binding 0）。
        unsigned int blockIndex = m_functions->glGetUniformBlockIndex(m_gizmoProgram, "UniformBufferObject");
        if (blockIndex != GL_INVALID_INDEX) {
            m_functions->glUniformBlockBinding(m_gizmoProgram, blockIndex, 0);
        }

        // 顶点属性：pos vec3（0）+ color vec4（1），步长 28 字节。
        m_functions->glGenVertexArrays(1, &m_gizmoVao);
        m_functions->glGenBuffers(1, &m_gizmoVbo);
        m_functions->glBindVertexArray(m_gizmoVao);
        m_functions->glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
        m_functions->glEnableVertexAttribArray(0);
        m_functions->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            sizeof(float) * 7, reinterpret_cast<const void*>(0));
        m_functions->glEnableVertexAttribArray(1);
        m_functions->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
            sizeof(float) * 7, reinterpret_cast<const void*>(sizeof(float) * 3));
        m_functions->glBindVertexArray(0);
        m_functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    } catch (const std::exception& e) {
        SetLastError(e.what());
        std::cerr << "OpenGL 读取 Gizmo 着色器失败: " << e.what() << std::endl;
        return false;
    }
}

// 销毁 Gizmo 程序与 VAO/VBO。
void GLRender3D::DestroyGizmoSupport() {
    if (m_functions) {
        if (m_gizmoVbo != 0) {
            m_functions->glDeleteBuffers(1, &m_gizmoVbo);
        }
        if (m_gizmoVao != 0) {
            m_functions->glDeleteVertexArrays(1, &m_gizmoVao);
        }
        if (m_gizmoProgram != 0) {
            m_functions->glDeleteProgram(m_gizmoProgram);
        }
    }
    m_gizmoVbo = 0;
    m_gizmoVao = 0;
    m_gizmoProgram = 0;
    m_gizmoVboCapacity = 0;
    m_gizmoVertices.clear();
    m_gizmoVertexCount = 0;
}

// 确保 Gizmo VAO/VBO 可容纳 vertexCount 个顶点。
bool GLRender3D::EnsureGizmoVertexBuffer(uint32_t vertexCount) {
    if (!m_functions || m_gizmoVbo == 0) return false;
    const std::size_t required = static_cast<std::size_t>(vertexCount) * sizeof(float) * 7;
    if (m_gizmoVboCapacity >= required) return true;

    // 预留余量，减少每帧重分配。
    const std::size_t capacity = required + sizeof(float) * 7 * 1024;
    m_functions->glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
    m_functions->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity),
                              nullptr, GL_DYNAMIC_DRAW);
    m_functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_gizmoVboCapacity = capacity;
    return true;
}

// 绘制 Gizmo 叠加层（无深度测试，直接输出顶点色）。
void GLRender3D::DrawGizmoOverlay() {
    if (!m_functions || m_gizmoProgram == 0 || m_gizmoVertexCount == 0) return;
    if (!EnsureGizmoVertexBuffer(m_gizmoVertexCount)) return;

    const size_t floatCount = static_cast<size_t>(m_gizmoVertexCount) * 7;

    // 叠加到默认帧缓冲：若场景画在 HDR / MSAA 目标，后处理已把结果输出到默认帧缓冲。
    m_functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_functions->glViewport(0, 0,
        static_cast<int>(m_framebufferWidth),
        static_cast<int>(m_framebufferHeight));
    m_functions->glDisable(GL_DEPTH_TEST);
    m_functions->glDisable(GL_BLEND);
    m_functions->glDisable(GL_CULL_FACE);

    m_functions->glUseProgram(m_gizmoProgram);
    m_currentProgram = m_gizmoProgram;
    // Gizmo 着色器直接读取 UBO 的 model/view/proj（与场景同一块）。
    m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);

    m_functions->glBindVertexArray(m_gizmoVao);
    m_functions->glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
    m_functions->glBufferSubData(GL_ARRAY_BUFFER, 0,
        static_cast<GLsizeiptr>(floatCount * sizeof(float)), m_gizmoVertices.data());
    m_functions->glDrawArrays(GL_LINES, 0, static_cast<int>(m_gizmoVertexCount));

    m_functions->glBindVertexArray(0);
    m_functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_functions->glUseProgram(m_program);
    m_currentProgram = m_program;
    m_functions->glEnable(GL_DEPTH_TEST);
    m_functions->glDepthFunc(GL_LESS);
}
