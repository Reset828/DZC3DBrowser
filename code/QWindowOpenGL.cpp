#include "QWindowOpenGL.h"
#include "Render/GLRender.h"
#include <QExposeEvent>
#include <QResizeEvent>
#include <QSurfaceFormat>

QWindowOpenGL::QWindowOpenGL(GLRender* renderer)
    : m_renderer(renderer)
    , m_context(nullptr)
    , m_initialized(false)
{
    QSurfaceFormat format;
    format.setVersion(4, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    // 关闭垂直同步：OpenGL 端不再被显示器刷新率限制，代价是可能出现撕裂。
    format.setSwapInterval(0);
    setFormat(format);
    setSurfaceType(QSurface::OpenGLSurface);
}

QWindowOpenGL::~QWindowOpenGL() {
    if (m_context) {
        m_context->doneCurrent();
    }
}

// 首次露出时创建后端并 Initialize。
void QWindowOpenGL::exposeEvent(QExposeEvent* event) {
    Q_UNUSED(event);
    if (isExposed() && !m_initialized) {
        if (!CreateOpenGLContext()) {
            if (m_renderer) {
                m_renderer->ReportError("创建 OpenGL 上下文失败");
            }
            emit openGLReady();
            return;
        }
        if (!m_renderer) return;

        m_initialized = true;

        const uint32_t w = static_cast<uint32_t>(width() * devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(height() * devicePixelRatio());

        m_renderer->SetContext(m_context);
        m_renderer->SetWindow(this);
        m_renderer->SetFramebufferSize(w, h);
        if (!m_renderer->Initialize("OpenGLReference", w, h)) {
            emit openGLReady();
            return;
        }

        emit openGLReady();
    }
}

// 窗口尺寸变化时更新帧缓冲大小。
void QWindowOpenGL::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    if (m_renderer && m_renderer->IsInitialized()) {
        const uint32_t w = static_cast<uint32_t>(event->size().width() * devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(event->size().height() * devicePixelRatio());
        m_renderer->SetFramebufferSize(w, h);
        if (w > 0 && h > 0) {
            m_renderer->SetFramebufferResized(true);
        }
    }
}

// 创建 Qt OpenGL 上下文。
bool QWindowOpenGL::CreateOpenGLContext() {
    if (m_context) return true;

    m_context = new QOpenGLContext(this);
    m_context->setFormat(requestedFormat());
    if (!m_context->create()) {
        delete m_context;
        m_context = nullptr;
        return false;
    }
    return true;
}

// 返回 Qt OpenGL 上下文。
QOpenGLContext* QWindowOpenGL::GetContext() const { return m_context; }

// 更换窗口绑定的渲染器。
void QWindowOpenGL::SetRenderer(GLRender* renderer) { m_renderer = renderer; }
