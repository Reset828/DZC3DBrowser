#ifndef __QWINDOW_OPENGL_H__
#define __QWINDOW_OPENGL_H__

#include <QWindow>
#include <QOpenGLContext>

class GLRender;

class QWindowOpenGL : public QWindow {
    Q_OBJECT
public:
    explicit QWindowOpenGL(GLRender* renderer);
    ~QWindowOpenGL();

    QOpenGLContext* GetContext() const;
    void SetRenderer(GLRender* renderer);

signals:
    void openGLReady();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool CreateOpenGLContext();

    GLRender* m_renderer;
    QOpenGLContext* m_context;
    bool m_initialized;
};

#endif // __QWINDOW_OPENGL_H__
