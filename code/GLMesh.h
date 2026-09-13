#pragma once

#include "Object/GLObject.h"
#include "VertexType/VertexTypes.h"
#include <vector>

class GLMesh : public GLObject {
public:
    GLMesh();
    ~GLMesh() override;

    // 同步上传 Mesh 数据。
    void SetMeshDataSync(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);

    // 绑定 VAO 并按当前模式绘制。
    void Render(int mode = 0) override;

private:
    bool m_buffersReady = false;
};
