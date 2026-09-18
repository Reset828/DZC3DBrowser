#include "AssetMeshUpload.h"

// 把资产网格转成渲染器使用的 Vertex3D 顶点与索引数组。
void BuildVertex3DArrays(const MeshData& mesh,
                         std::vector<Vertex3D>& outVertices,
                         std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outVertices.reserve(mesh.vertices.size());
    for (const AssetVertex& source : mesh.vertices) {
        Vertex3D vertex{};
        vertex.position[0] = source.position[0];
        vertex.position[1] = source.position[1];
        vertex.position[2] = source.position[2];
        vertex.color[0] = source.color[0];
        vertex.color[1] = source.color[1];
        vertex.color[2] = source.color[2];
        vertex.texCoord[0] = source.texCoord[0];
        vertex.texCoord[1] = source.texCoord[1];
        vertex.normal[0] = source.normal[0];
        vertex.normal[1] = source.normal[1];
        vertex.normal[2] = source.normal[2];
        vertex.tangent[0] = source.tangent[0];
        vertex.tangent[1] = source.tangent[1];
        vertex.tangent[2] = source.tangent[2];
        vertex.tangent[3] = source.tangent[3];
        outVertices.push_back(vertex);
    }
    outIndices = mesh.indices;
}
