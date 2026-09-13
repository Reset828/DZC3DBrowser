#include "GLMesh.h"
#include "Render/GLRender.h"
#include <QOpenGLFunctions_4_2_Core>
#include <cstddef>

GLMesh::GLMesh() {
    m_uType = OT_OBJECT;
}

GLMesh::~GLMesh() {}

// 同步上传 Mesh 数据。
void GLMesh::SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                                 const std::vector<uint32_t>& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    DestroyBuffers();
    m_buffersReady = false;

    CreateVertexBuffer(vertices.data(), vertices.size() * sizeof(Vertex3D));

    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (gl && m_vao != 0) {
        gl->glBindVertexArray(m_vao);
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

    CreateIndexBuffer(indices.data(), indices.size() * sizeof(uint32_t),
                      static_cast<uint32_t>(indices.size()));
    m_buffersReady = (m_vao != 0 && m_indexCount > 0);
}

// 绑定 VAO 并按当前模式绘制。
void GLMesh::Render(int mode) {
    if (!IsVisible() || !m_buffersReady || !m_pRender) return;
    if (m_indexCount == 0 || m_vao == 0) return;

    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    if (mode == Object::RM_SHADOW) {
        const unsigned int shadowProgram = m_pRender->GetShadowProgram();
        if (shadowProgram == 0) return;
        gl->glUseProgram(shadowProgram);
        m_pRender->SetPolygonWireframe(false);
        gl->glBindVertexArray(m_vao);
        m_pRender->DrawIndexed(m_indexCount);
        gl->glBindVertexArray(0);
        return;
    }

    m_pRender->SetPolygonWireframe(m_pRender->IsWireframeEnabled());
    gl->glBindVertexArray(m_vao);
    m_pRender->DrawIndexed(m_indexCount);
    gl->glBindVertexArray(0);
    m_pRender->SetPolygonWireframe(false);
}
