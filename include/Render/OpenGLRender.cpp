#include "OpenGLRender.h"
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

OpenGLRender::OpenGLRender() {}

OpenGLRender::~OpenGLRender() {
    Shutdown();
}

bool OpenGLRender::Initialize(const char* /*appName*/, uint32_t width, uint32_t height) {
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

void OpenGLRender::Quiesce() {
    if (!m_initialized) return;

    m_shuttingDown = true;
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void OpenGLRender::Shutdown() {
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

void OpenGLRender::SetClearColor(float r, float g, float b, float a) {
    m_clearColor = { r, g, b, a };
}

bool OpenGLRender::BeginFrame() {
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

void OpenGLRender::EndFrame() {
    if (!m_initialized || !m_context || !m_window) return;
    OnEndFrame();
    m_context->swapBuffers(m_window);
}

void OpenGLRender::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    if (!m_functions || indexCount == 0) return;
    m_functions->glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indexCount),
        GL_UNSIGNED_INT, nullptr, static_cast<int>(instanceCount));
}

void OpenGLRender::SetPolygonWireframe(bool enabled) {
    if (!m_functions) return;
    m_functions->glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
}

QOpenGLFunctions_4_2_Core* OpenGLRender::GetFunctions() const { return m_functions; }
unsigned int OpenGLRender::GetCurrentProgram() const { return m_currentProgram; }

void OpenGLRender::WaitForIdle() {}

void OpenGLRender::SubmitAsync(QRunnable* task) {
    QThreadPool::globalInstance()->start(task);
}

bool OpenGLRender::IsShuttingDown() const { return m_shuttingDown; }

bool OpenGLRender::IsInitialized() const { return m_initialized; }

void OpenGLRender::SetContext(QOpenGLContext* context) {
    m_context = context;
}

void OpenGLRender::SetWindow(QWindow* window) {
    m_window = window;
}

QOpenGLContext* OpenGLRender::GetContext() const { return m_context; }
QWindow* OpenGLRender::GetWindow() const { return m_window; }

void OpenGLRender::SetFramebufferSize(uint32_t width, uint32_t height) {
    m_framebufferWidth = width;
    m_framebufferHeight = height;
}

void OpenGLRender::SetFramebufferResized(bool resized) {
    m_framebufferResized = resized;
}

void OpenGLRender::OnMouseDown(float nx, float ny, int button) {
    (void)nx;
    (void)ny;
    (void)button;
}

void OpenGLRender::OnMouseMove(float nx, float ny) {
    (void)nx;
    (void)ny;
}

void OpenGLRender::OnMouseUp(int button) {
    (void)button;
}

void OpenGLRender::OnMouseWheel(float delta) {
    (void)delta;
}

bool OpenGLRender::CreateRenderPass() { return true; }
bool OpenGLRender::CreatePipelines() { return true; }
bool OpenGLRender::CreateFramebuffers() { return true; }

std::vector<char> OpenGLRender::ReadShaderFile(const std::string& filename) {
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
