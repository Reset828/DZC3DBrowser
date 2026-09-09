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
#include <iostream>

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
    if (m_functions) {
        m_functions->glEnable(GL_DEPTH_TEST);
        m_functions->glDepthFunc(GL_LESS);
        m_functions->glDisable(GL_CULL_FACE);
    }
    return true;
}

void OpenGLRender3D::OnShutdown() {
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
        return m_program != 0;
    } catch (const std::exception& e) {
        std::cerr << "OpenGL 读取着色器失败: " << e.what() << std::endl;
        return false;
    }
}

void OpenGLRender3D::DestroyShaderProgram() {
    if (m_functions && m_program != 0) {
        m_functions->glDeleteProgram(m_program);
    }
    m_program = 0;
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
}

void OpenGLRender3D::SetShadowTextureSize(uint32_t size) {
    m_shadowTextureSize = size;
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
