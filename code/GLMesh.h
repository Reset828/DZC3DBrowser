#pragma once

#include "Object/GLObject.h"
#include "Asset/MeshData.h"
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

    // 绑定 VAO 并按当前模式绘制。
    void Render(int mode = 0) override;

private:
    bool m_buffersReady = false;
};
