#include "GLMesh.h"
#include "AssetMeshUpload.h"
#include "Render/GLRender.h"
#include "Math/Transform.h"
#include <QOpenGLFunctions_4_2_Core>
#include <cstddef>

GLMesh::GLMesh() {
    m_uType = OT_OBJECT;
}

GLMesh::~GLMesh() {}

// 从资产网格上传（内部拍平为 Vertex3D）。
void GLMesh::SetMeshDataSync(const MeshData& mesh) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    BuildVertex3DArrays(mesh, vertices, indices);
    SetMeshDataSync(vertices, indices);
}

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
        gl->glEnableVertexAttribArray(4);
        gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
            reinterpret_cast<const void*>(offsetof(Vertex3D, tangent)));
        gl->glBindVertexArray(0);
    }

    CreateIndexBuffer(indices.data(), indices.size() * sizeof(uint32_t),
                      static_cast<uint32_t>(indices.size()));
    m_buffersReady = (m_vao != 0 && m_indexCount > 0);
}

// 设置逐 SubMesh 绘制信息。
void GLMesh::SetSubMeshDrawInfos(std::vector<SubMeshDrawInfo>&& infos) {
    m_subMeshInfos = std::move(infos);
}

// 切换某材质的可见性（同材质索引的所有 SubMesh 一起）。
void GLMesh::SetMaterialVisible(int materialIndex, bool visible) {
    for (SubMeshDrawInfo& info : m_subMeshInfos) {
        if (info.materialIndex == materialIndex) {
            info.visible = visible;
        }
    }
}

// 绑定 VAO 并按当前模式逐 SubMesh 绘制。
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
        // 逐对象世界矩阵（任务 2.1）：阴影通道也需要。
        m_pRender->SetObjectModelMatrix(GetWorldMatrix().m[0]);
        // 阴影通道逐 SubMesh 绘制，但材质可见性仍生效。
        if (m_subMeshInfos.empty()) {
            m_pRender->DrawIndexed(m_indexCount);
        } else {
            for (const SubMeshDrawInfo& info : m_subMeshInfos) {
                if (!info.visible || info.indexCount == 0) continue;
                m_pRender->DrawIndexedRange(info.indexCount, info.indexOffset);
            }
        }
        gl->glBindVertexArray(0);
        return;
    }

    m_pRender->SetPolygonWireframe(m_pRender->IsWireframeEnabled());
    gl->glBindVertexArray(m_vao);

    if (m_subMeshInfos.empty()) {
        // 兼容旧路径：整网格一次绘制（应用默认材质）。
        // ApplyMaterial 会绑定主程序，之后才能写入 uObjectModel（uniform 属于程序）。
        m_pRender->ApplyMaterial(MaterialParams{}, MaterialTextureSet{});
        m_pRender->SetObjectModelMatrix(GetWorldMatrix().m[0]);
        m_pRender->DrawIndexed(m_indexCount);
    } else {
        for (size_t i = 0; i < m_subMeshInfos.size(); ++i) {
            const SubMeshDrawInfo& info = m_subMeshInfos[i];
            if (!info.visible || info.indexCount == 0) continue;
            const bool blend =
                info.material.alphaMode == static_cast<int>(MaterialAlphaMode::Blend);
            if (blend) {
                // 透明 SubMesh 交给渲染器收集，主通道结束前统一排序绘制。
                // 排序中心需先经逐对象世界矩阵变换到归一化场景空间。
                const Vec3 center = TransformPoint(
                    GetWorldMatrix(), Vec3{ info.center[0], info.center[1], info.center[2] });
                const float centerArray[3] = { center.x, center.y, center.z };
                float distance = m_pRender->ComputeDrawDistance(centerArray);
                m_pRender->QueueTransparentDraw(this, static_cast<int>(i), distance);
                continue;
            }
            DrawSubMeshImpl(info);
        }
    }

    gl->glBindVertexArray(0);
    m_pRender->SetPolygonWireframe(false);
}

// 只绘制指定的 SubMesh（透明排序 flush 时逐个调用）。
void GLMesh::DrawSubMesh(int subMeshIndex, int iMode) {
    if (!IsVisible() || !m_buffersReady || !m_pRender) return;
    if (subMeshIndex < 0 || subMeshIndex >= static_cast<int>(m_subMeshInfos.size())) return;
    const SubMeshDrawInfo& info = m_subMeshInfos[static_cast<size_t>(subMeshIndex)];
    if (!info.visible || info.indexCount == 0) return;

    QOpenGLFunctions_4_2_Core* gl = m_pRender->GetFunctions();
    if (!gl) return;
    gl->glBindVertexArray(m_vao);
    DrawSubMeshImpl(info);
    gl->glBindVertexArray(0);
}

// 绘制单个 SubMesh：应用材质 -> 逐对象矩阵 -> 范围绘制。
// 透明混合状态由 FlushTransparentDraws 统一开关，这里无需再判断。
void GLMesh::DrawSubMeshImpl(const SubMeshDrawInfo& info) {
    m_pRender->ApplyMaterial(info.material, info.textures);
    // ApplyMaterial 绑定主程序后再写入 uObjectModel（uniform 属于程序）。
    m_pRender->SetObjectModelMatrix(GetWorldMatrix().m[0]);
    m_pRender->DrawIndexedRange(info.indexCount, info.indexOffset);
}
