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
    setFormat(format);
    setSurfaceType(QSurface::OpenGLSurface);
}

QWindowOpenGL::~QWindowOpenGL() {
    if (m_context) {
        m_context->doneCurrent();
    }
}

void QWindowOpenGL::exposeEvent(QExposeEvent* event) {
    Q_UNUSED(event);
    if (isExposed() && !m_initialized) {
        if (!CreateOpenGLContext()) return;
        if (!m_renderer) return;

        m_initialized = true;

        const uint32_t w = static_cast<uint32_t>(width() * devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(height() * devicePixelRatio());

        m_renderer->SetContext(m_context);
        m_renderer->SetWindow(this);
        m_renderer->SetFramebufferSize(w, h);
        m_renderer->Initialize("OpenGLReference", w, h);

        emit openGLReady();
    }
}

void QWindowOpenGL::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    if (m_renderer && m_renderer->IsInitialized()) {
        const uint32_t w = static_cast<uint32_t>(event->size().width() * devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(event->size().height() * devicePixelRatio());
        m_renderer->SetFramebufferSize(w, h);
        m_renderer->SetFramebufferResized(true);
    }
}

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

QOpenGLContext* QWindowOpenGL::GetContext() const { return m_context; }

void QWindowOpenGL::SetRenderer(GLRender* renderer) { m_renderer = renderer; }
