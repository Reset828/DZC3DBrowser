#include "GLObject.h"
#include "Render/GLRender.h"
#include <QOpenGLFunctions_4_2_Core>
#include <cstddef>

GLObject::GLObject() {}

GLObject::~GLObject() {
    DestroyBuffers();
}

// 绑定所属渲染器。
void GLObject::SetRender(GLRender* pRender) {
    m_pRender = pRender;
}

// 返回所属渲染器。
GLRender* GLObject::GetRender() const {
    return m_pRender;
}

// 创建并上传顶点缓冲区。
void GLObject::CreateVertexBuffer(const void* data, std::size_t size) {
    if (!m_pRender || !data || size == 0) return;
    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    if (m_vao == 0) {
        gl->glGenVertexArrays(1, &m_vao);
    }
    if (m_vbo == 0) {
        gl->glGenBuffers(1, &m_vbo);
    }

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    gl->glBindVertexArray(0);
}

// 创建并上传索引缓冲区。
void GLObject::CreateIndexBuffer(const void* data, std::size_t size,
                                 uint32_t indexCount) {
    if (!m_pRender || !data || size == 0 || indexCount == 0) return;
    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    m_indexCount = indexCount;
    if (m_vao == 0) {
        gl->glGenVertexArrays(1, &m_vao);
    }
    if (m_ebo == 0) {
        gl->glGenBuffers(1, &m_ebo);
    }

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    gl->glBindVertexArray(0);
}

// 销毁对象持有的 GPU 缓冲区。
void GLObject::DestroyBuffers() {
    if (!m_pRender) return;
    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    if (m_ebo != 0) {
        gl->glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vbo != 0) {
        gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_indexCount = 0;
}

// 返回 VAO。
uint32_t GLObject::GetVertexArray() const { return m_vao; }
// 返回索引数量。
uint32_t GLObject::GetIndexCount() const { return m_indexCount; }
