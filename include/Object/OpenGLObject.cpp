#include "OpenGLObject.h"
#include "Render/OpenGLRender.h"
#include <QOpenGLFunctions_4_2_Core>
#include <cstddef>

OpenGLObject::OpenGLObject() {}

OpenGLObject::~OpenGLObject() {
    DestroyBuffers();
}

void OpenGLObject::SetRender(OpenGLRender* pRender) {
    m_pRender = pRender;
}

OpenGLRender* OpenGLObject::GetRender() const {
    return m_pRender;
}

void OpenGLObject::CreateVertexBuffer(const std::vector<Vertex3D>& vertices) {
    if (!m_pRender || vertices.empty()) return;
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
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex3D)),
        vertices.data(), GL_STATIC_DRAW);

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
        reinterpret_cast<const void*>(offsetof(Vertex3D, position)));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
        reinterpret_cast<const void*>(offsetof(Vertex3D, color)));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
        reinterpret_cast<const void*>(offsetof(Vertex3D, texCoord)));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
        reinterpret_cast<const void*>(offsetof(Vertex3D, normal)));

    gl->glBindVertexArray(0);
}

void OpenGLObject::CreateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (!m_pRender || indices.empty()) return;
    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    m_indexCount = static_cast<uint32_t>(indices.size());
    if (m_vao == 0) {
        gl->glGenVertexArrays(1, &m_vao);
    }
    if (m_ebo == 0) {
        gl->glGenBuffers(1, &m_ebo);
    }

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
        indices.data(), GL_STATIC_DRAW);
    gl->glBindVertexArray(0);
}

void OpenGLObject::DestroyBuffers() {
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

uint32_t OpenGLObject::GetVertexArray() const { return m_vao; }
uint32_t OpenGLObject::GetIndexCount() const { return m_indexCount; }
