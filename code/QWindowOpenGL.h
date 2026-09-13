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

    // 返回 Qt OpenGL 上下文。
    QOpenGLContext* GetContext() const;
    // 更换窗口绑定的渲染器。
    void SetRenderer(GLRender* renderer);

signals:
    // OpenGL 上下文就绪后发出。
    void openGLReady();

protected:
    // 首次露出时创建后端并 Initialize。
    void exposeEvent(QExposeEvent* event) override;
    // 窗口尺寸变化时更新帧缓冲大小。
    void resizeEvent(QResizeEvent* event) override;

private:
    // 创建 Qt OpenGL 上下文。
    bool CreateOpenGLContext();

    GLRender* m_renderer;
    QOpenGLContext* m_context;
    bool m_initialized;
};

#endif // __QWINDOW_OPENGL_H__
