#include "Asset/TangentGenerator.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

// 三维向量（局部使用，避免依赖 glm）。
struct Vec3f { float x, y, z; };

Vec3f Sub(const Vec3f& a, const Vec3f& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

float Dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f Cross(const Vec3f& a, const Vec3f& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float LengthSquared(const Vec3f& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

// 归一化；退化时返回 fallback。
Vec3f NormalizeOr(const Vec3f& v, const Vec3f& fallback) {
    const float lenSq = LengthSquared(v);
    if (!std::isfinite(lenSq) || lenSq <= 1.0e-20f) {
        return fallback;
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return { v.x * inv, v.y * inv, v.z * inv };
}

}  // 匿名命名空间

namespace TangentGenerator {

// 就地填充每个顶点的切线（xyz）+ 手性（w）。
void GenerateTangents(MeshData& mesh) {
    const size_t vertexCount = mesh.vertices.size();
    if (vertexCount == 0 || mesh.indices.size() < 3) {
        return;
    }

    std::vector<Vec3f> tangentSum(vertexCount, Vec3f{ 0.0f, 0.0f, 0.0f });
    std::vector<Vec3f> bitangentSum(vertexCount, Vec3f{ 0.0f, 0.0f, 0.0f });

    // 1~2. 逐三角形解算并累加。
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t i0 = mesh.indices[i];
        const uint32_t i1 = mesh.indices[i + 1];
        const uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }
        const AssetVertex& v0 = mesh.vertices[i0];
        const AssetVertex& v1 = mesh.vertices[i1];
        const AssetVertex& v2 = mesh.vertices[i2];

        const Vec3f p0{ v0.position[0], v0.position[1], v0.position[2] };
        const Vec3f p1{ v1.position[0], v1.position[1], v1.position[2] };
        const Vec3f p2{ v2.position[0], v2.position[1], v2.position[2] };

        const Vec3f edge1 = Sub(p1, p0);
        const Vec3f edge2 = Sub(p2, p0);

        const float duv1x = v1.texCoord[0] - v0.texCoord[0];
        const float duv1y = v1.texCoord[1] - v0.texCoord[1];
        const float duv2x = v2.texCoord[0] - v0.texCoord[0];
        const float duv2y = v2.texCoord[1] - v0.texCoord[1];

        const float determinant = duv1x * duv2y - duv2x * duv1y;
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-20f) {
            continue;  // 该面 UV 退化，跳过（用其它面或兜底）
        }
        const float r = 1.0f / determinant;

        const Vec3f tangent{
            (edge1.x * duv2y - edge2.x * duv1y) * r,
            (edge1.y * duv2y - edge2.y * duv1y) * r,
            (edge1.z * duv2y - edge2.z * duv1y) * r
        };
        const Vec3f bitangent{
            (edge2.x * duv1x - edge1.x * duv2x) * r,
            (edge2.y * duv1x - edge1.y * duv2x) * r,
            (edge2.z * duv1x - edge1.z * duv2x) * r
        };

        tangentSum[i0].x += tangent.x; tangentSum[i0].y += tangent.y; tangentSum[i0].z += tangent.z;
        tangentSum[i1].x += tangent.x; tangentSum[i1].y += tangent.y; tangentSum[i1].z += tangent.z;
        tangentSum[i2].x += tangent.x; tangentSum[i2].y += tangent.y; tangentSum[i2].z += tangent.z;

        bitangentSum[i0].x += bitangent.x; bitangentSum[i0].y += bitangent.y; bitangentSum[i0].z += bitangent.z;
        bitangentSum[i1].x += bitangent.x; bitangentSum[i1].y += bitangent.y; bitangentSum[i1].z += bitangent.z;
        bitangentSum[i2].x += bitangent.x; bitangentSum[i2].y += bitangent.y; bitangentSum[i2].z += bitangent.z;
    }

    // 3~4. 逐顶点正交化并写出手性。
    const Vec3f kFallbackTangent{ 1.0f, 0.0f, 0.0f };
    for (size_t v = 0; v < vertexCount; ++v) {
        AssetVertex& vertex = mesh.vertices[v];
        const Vec3f normal = NormalizeOr(
            Vec3f{ vertex.normal[0], vertex.normal[1], vertex.normal[2] },
            Vec3f{ 0.0f, 0.0f, 1.0f });

        Vec3f tangent = NormalizeOr(tangentSum[v], kFallbackTangent);
        // Gram-Schmidt：去掉切线在法线方向的分量。
        const float projection = Dot(normal, tangent);
        tangent = NormalizeOr(
            Vec3f{ tangent.x - normal.x * projection,
                   tangent.y - normal.y * projection,
                   tangent.z - normal.z * projection },
            kFallbackTangent);

        const Vec3f bitangent = bitangentSum[v];
        const Vec3f computedBitangent = Cross(normal, tangent);
        const float hand = Dot(computedBitangent, bitangent) < 0.0f ? -1.0f : 1.0f;

        vertex.tangent[0] = tangent.x;
        vertex.tangent[1] = tangent.y;
        vertex.tangent[2] = tangent.z;
        vertex.tangent[3] = hand;
    }
}

}  // namespace TangentGenerator
