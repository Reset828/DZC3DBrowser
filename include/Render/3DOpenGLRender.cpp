#include "3DOpenGLRender.h"
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

OpenGLRender3D::OpenGLRender3D() {
    UpdateSunDirection();
}

OpenGLRender3D::~OpenGLRender3D() {}

void OpenGLRender3D::OnMouseDown(float nx, float ny, int button) {
    m_mouseButton = button;
    m_lastMouse = glm::vec2(nx, ny);
}

void OpenGLRender3D::OnMouseMove(float nx, float ny) {
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

glm::vec3 OpenGLRender3D::ProjectToVirtualSphere(float nx, float ny) const {
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

void OpenGLRender3D::ApplyConstrainedLocalRotation(const glm::vec3& localAxis,
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

void OpenGLRender3D::OnMouseUp(int /*button*/) {
    m_mouseButton = -1;
}

void OpenGLRender3D::OnMouseWheel(float delta) {
    m_orbitDistance *= (delta > 0.0f) ? 0.9f : 1.1f;
    m_orbitDistance = glm::clamp(m_orbitDistance, 0.1f, 1000.0f);
}

bool OpenGLRender3D::OnInitialize() {
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

void OpenGLRender3D::OnShutdown() {
    DestroyShadowMap();
    DestroyDummyShadowMap();
    DestroyDepthReadbackResources();
    DestroyUniformBuffers();
    DestroyShaderProgram();
    DestroyDepthResources();
}

void OpenGLRender3D::OnBeginFrame() {
    ProcessDepthReadback();
    if (m_functions && m_program != 0) {
        m_functions->glUseProgram(m_program);
        m_currentProgram = m_program;
        m_functions->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    }
    BindShadowTexture();
    UpdateUniformBuffer();
}

void OpenGLRender3D::OnEndFrame() {}

void OpenGLRender3D::OnRecreateSwapchain() {
    DestroyDepthResources();
    CreateDepthResources();
}

bool OpenGLRender3D::CreateRenderPass() { return true; }
bool OpenGLRender3D::CreatePipelines() { return true; }
bool OpenGLRender3D::CreateFramebuffers() { return true; }

unsigned int OpenGLRender3D::CompileShader(unsigned int type, const char* source) {
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

unsigned int OpenGLRender3D::LinkProgram(unsigned int vert, unsigned int frag) {
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

bool OpenGLRender3D::CreateShaderProgram() {
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

void OpenGLRender3D::DestroyShaderProgram() {
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

bool OpenGLRender3D::CreateUniformBuffers() {
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

void OpenGLRender3D::DestroyUniformBuffers() {
    if (m_functions && m_ubo != 0) {
        m_functions->glDeleteBuffers(1, &m_ubo);
    }
    m_ubo = 0;
}

void OpenGLRender3D::UpdateUniformBuffer() {
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
bool OpenGLRender3D::CreateDepthResources() { return true; }
void OpenGLRender3D::DestroyDepthResources() {}
bool OpenGLRender3D::CreateDepthReadbackResources() { return true; }
void OpenGLRender3D::DestroyDepthReadbackResources() {}
void OpenGLRender3D::ProcessDepthReadback() {}

void OpenGLRender3D::SetWireframeEnabled(bool enabled) {
    m_wireframeMode = enabled;
}

void OpenGLRender3D::SetGrayEnabled(bool enabled) {
    m_grayEnabled = enabled;
}

void OpenGLRender3D::SetDyeEnabled(bool enabled) {
    m_dyeEnabled = enabled;
}

void OpenGLRender3D::SetLightAnalysisEnabled(bool enabled) {
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

void OpenGLRender3D::SetShadowTextureSize(uint32_t size) {
    m_shadowTextureSize = size;
    if (m_initialized && m_lightAnalysisEnabled) {
        if (m_context && m_window) {
            m_context->makeCurrent(m_window);
        }
        EnsureShadowMapForAnalysis();
    }
}

uint32_t OpenGLRender3D::GetShadowTextureSize() const {
    return m_shadowTextureSize;
}

bool OpenGLRender3D::IsShadowMapReady() const {
    return m_shadowMapReady;
}

std::string OpenGLRender3D::TakeShadowMapStatus() {
    std::string message = std::move(m_shadowMapStatus);
    m_shadowMapStatus.clear();
    return message;
}

void OpenGLRender3D::SetShadowSceneBounds(const Vec3& boundsMin, const Vec3& boundsMax, bool valid) {
    m_shadowBoundsValid = valid;
    m_shadowBoundsMin = glm::vec3(boundsMin.x, boundsMin.y, boundsMin.z);
    m_shadowBoundsMax = glm::vec3(boundsMax.x, boundsMax.y, boundsMax.z);
}

void OpenGLRender3D::SetShadowMapStatus(const std::string& message) {
    m_shadowMapStatus = message;
}

bool OpenGLRender3D::ShouldRenderShadows() const {
    return m_lightAnalysisEnabled && m_sunAboveHorizon && m_shadowMapReady && !m_shadowPassActive;
}

glm::mat4 OpenGLRender3D::ComputeLightViewProj() const {
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

void OpenGLRender3D::BindShadowTexture() {
    if (!m_functions) return;
    m_functions->glActiveTexture(GL_TEXTURE1);
    unsigned int texture = m_dummyShadowTexture;
    if (ShouldRenderShadows() && m_shadowTexture != 0) {
        texture = m_shadowTexture;
    }
    m_functions->glBindTexture(GL_TEXTURE_2D, texture);
    m_functions->glActiveTexture(GL_TEXTURE0);
}

bool OpenGLRender3D::CreateDummyShadowMap() {
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

void OpenGLRender3D::DestroyDummyShadowMap() {
    if (m_functions && m_dummyShadowTexture != 0) {
        m_functions->glDeleteTextures(1, &m_dummyShadowTexture);
    }
    m_dummyShadowTexture = 0;
}

bool OpenGLRender3D::CreateShadowMap(uint32_t size) {
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

void OpenGLRender3D::DestroyShadowMap() {
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

bool OpenGLRender3D::TryAllocateShadowMap(uint32_t size) {
    DestroyShadowMap();
    if (!CreateShadowMap(size)) return false;
    m_shadowMapReady = true;
    m_allocatedShadowTextureSize = size;
    return true;
}

bool OpenGLRender3D::EnsureShadowMapForAnalysis() {
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

bool OpenGLRender3D::BeginShadowPass() {
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

void OpenGLRender3D::EndShadowPass() {
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

void OpenGLRender3D::SetLatitude(float latitude) {
    m_latitude = latitude;
    UpdateSunDirection();
}

void OpenGLRender3D::SetLightDate(int year, int month, int day) {
    m_lightYear = year;
    m_lightMonth = month;
    m_lightDay = day;
    UpdateSunDirection();
}

void OpenGLRender3D::SetLightTimeMinutes(int minutes) {
    m_lightTimeMinutes = minutes;
    UpdateSunDirection();
}

glm::vec3 OpenGLRender3D::GetSunDirection() const {
    return m_sunDirection;
}

bool OpenGLRender3D::IsSunAboveHorizon() const {
    return m_sunAboveHorizon;
}

void OpenGLRender3D::UpdateSunDirection() {
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

void OpenGLRender3D::SetOrthographicEnabled(bool enabled) {
    if (enabled && !m_orthographicEnabled) {
        m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        m_mouseButton = -1;
        m_lastMouse = glm::vec2(0.0f);
        m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    m_orthographicEnabled = enabled;
}

void OpenGLRender3D::SetOrbitCenter(const Vec3& normalizedCenter) {
    m_orbitCenter = glm::vec3(
        normalizedCenter.x, normalizedCenter.y, normalizedCenter.z);
}

void OpenGLRender3D::ResetView(float orbitDistance) {
    m_modelRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_panOffset = glm::vec3(0.0f);
    m_orbitDistance = std::isfinite(orbitDistance)
        ? glm::clamp(orbitDistance, 0.1f, 1000.0f)
        : 3.0f;
    m_mouseButton = -1;
    m_lastMouse = glm::vec2(0.0f);
    m_lastVerticalLocalAxis = glm::vec3(1.0f, 0.0f, 0.0f);
}

void OpenGLRender3D::SetCoordinateNormalization(const Vec3& sourceCenter,
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

void OpenGLRender3D::RequestCoordReadback(float ndcX, float ndcY) {
    m_depthReadbackRequested = true;
    m_requestedNDCX = ndcX;
    m_requestedNDCY = ndcY;
}

bool OpenGLRender3D::HasNewWorldCoord() const {
    bool v = m_newCoordAvailable;
    m_newCoordAvailable = false;
    return v;
}

float OpenGLRender3D::GetLastWorldX() const { return m_lastWorldCoord[0]; }
float OpenGLRender3D::GetLastWorldY() const { return m_lastWorldCoord[1]; }
float OpenGLRender3D::GetLastWorldZ() const { return m_lastWorldCoord[2]; }
