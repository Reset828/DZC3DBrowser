#include "OpenGLMesh.h"
#include "Render/GLRender.h"
#include <QOpenGLFunctions_4_2_Core>

OpenGLMesh::OpenGLMesh() {
    m_uType = OT_OBJECT;
}

OpenGLMesh::~OpenGLMesh() {}

void OpenGLMesh::SetMeshDataSync(const std::vector<Vertex3D>& vertices,
                                 const std::vector<uint32_t>& indices) {
    if (!m_pRender || vertices.empty() || indices.empty()) return;

    DestroyBuffers();
    m_buffersReady = false;

    CreateVertexBuffer(vertices);
    CreateIndexBuffer(indices);
    m_buffersReady = (m_vao != 0 && m_indexCount > 0);
}

void OpenGLMesh::Render(int mode) {
    if (!IsVisible() || !m_buffersReady || !m_pRender) return;
    if (m_indexCount == 0 || m_vao == 0) return;

    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;

    if (mode == SceneObject::RM_SHADOW) {
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
