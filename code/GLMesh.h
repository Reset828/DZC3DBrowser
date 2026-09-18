#pragma once

#include "Object/GLObject.h"
#include "Asset/MeshData.h"
#include "MeshDrawInfo.h"
#include "VertexType/VertexTypes.h"
#include <vector>

class GLMesh : public GLObject {
public:
    GLMesh();
    ~GLMesh() override;

    // 从资产网格上传（内部拍平为 Vertex3D）。
    void SetMeshDataSync(const MeshData& mesh);

    // 同步上传 Mesh 数据。
    void SetMeshDataSync(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);

    // 设置逐 SubMesh 绘制信息（材质、纹理句柄、可见性）。
    void SetSubMeshDrawInfos(std::vector<SubMeshDrawInfo>&& infos);

    // 切换某材质的可见性（材质面板）。
    void SetMaterialVisible(int materialIndex, bool visible);

    // 绑定 VAO 并按当前模式逐 SubMesh 绘制。
    void Render(int mode = 0) override;

    // 只绘制指定的 SubMesh（透明排序 flush 时逐个调用）。
    void DrawSubMesh(int subMeshIndex, int iMode = RM_DEFAULT) override;

private:
    // 绘制单个 SubMesh 的公共实现。
    void DrawSubMeshImpl(const SubMeshDrawInfo& info);

    bool m_buffersReady = false;
    std::vector<SubMeshDrawInfo> m_subMeshInfos;
};
