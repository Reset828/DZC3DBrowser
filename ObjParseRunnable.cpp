#include "ObjParseRunnable.h"
#include <QApplication>
#include <QMetaObject>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <cmath>
#include <limits>

ObjParseRunnable::ObjParseRunnable(std::string filePath, Callback callback)
    : m_filePath(std::move(filePath))
    , m_callback(std::move(callback))
{
    setAutoDelete(true);
}

ObjParseRunnable::~ObjParseRunnable() = default;

void ObjParseRunnable::run() {
    // --- 1. 二进制读入整个文件 ---
    std::ifstream file(m_filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开 OBJ 文件: " + m_filePath);
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> content(static_cast<size_t>(fileSize));
    if (!file.read(content.data(), fileSize)) {
        throw std::runtime_error("读取 OBJ 文件失败: " + m_filePath);
    }
    file.close();

    // --- 2. 解析 ---
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(fileSize / 64);
    indices.reserve(fileSize / 64);

    const char* ptr = content.data();
    const char* end = ptr + content.size();

    // --- 2a. 辅助解析函数 ---
    auto skipWhitespace = [&ptr, end]() {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
    };

    auto parseFloat = [&ptr, end, &skipWhitespace]() -> float {
        skipWhitespace();
        char* endPtr = nullptr;
        float val = static_cast<float>(std::strtod(ptr, &endPtr));
        ptr = endPtr;
        return val;
    };

    auto parseInt = [&ptr, end, &skipWhitespace]() -> int {
        skipWhitespace();
        char* endPtr = nullptr;
        int val = static_cast<int>(std::strtol(ptr, &endPtr, 10));
        ptr = endPtr;
        return val;
    };

    // 解析 face 中单个顶点索引，支持 v、v/vt、v/vt/vn、v//vn、v/ 等格式
    auto parseFaceIndex = [&ptr, end, &parseInt]() -> int {
        int val = parseInt();
        if (ptr < end && *ptr == '/') {
            ++ptr;
            while (ptr < end && *ptr != '/' && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r') ++ptr;
            if (ptr < end && *ptr == '/') {
                ++ptr;
                while (ptr < end && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r') ++ptr;
            }
        }
        return val;
    };

    // --- 2b. 三角剖分（耳切法 Ear-clipping，支持凹多边形） ---
    auto triangulateFace = [&vertices, &indices](const std::vector<int>& face) {
        int n = (int)face.size();
        if (n < 3) return;
        if (n == 3) {
            indices.push_back(static_cast<uint32_t>(face[0] - 1));
            indices.push_back(static_cast<uint32_t>(face[1] - 1));
            indices.push_back(static_cast<uint32_t>(face[2] - 1));
            return;
        }

        // 获取顶点位置的便捷函数（OBJ 索引 1-based → vector 0-based）
        auto getPos = [&](int objIdx) -> const float* {
            return vertices[objIdx - 1].position;
        };

        // 计算法线 → 确定 3D→2D 投影丢弃的轴（法线最大分量为投影平面法线）
        const float* p0 = getPos(face[0]);
        const float* p1 = getPos(face[1]);
        const float* p2 = getPos(face[2]);
        float nx = (p1[1] - p0[1]) * (p2[2] - p0[2]) - (p1[2] - p0[2]) * (p2[1] - p0[1]);
        float ny = (p1[2] - p0[2]) * (p2[0] - p0[0]) - (p1[0] - p0[0]) * (p2[2] - p0[2]);
        float nz = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);

        int dropAxis = 0;
        float absNx = std::abs(nx), absNy = std::abs(ny), absNz = std::abs(nz);
        if (absNy >= absNx && absNy >= absNz) dropAxis = 1;
        else if (absNz >= absNx && absNz >= absNy) dropAxis = 2;

        struct Vec2 { float x, y; };
        auto project = [dropAxis](const float* p) -> Vec2 {
            if (dropAxis == 0) return {p[1], p[2]};
            if (dropAxis == 1) return {p[0], p[2]};
            return {p[0], p[1]};
        };

        // 投影所有顶点到 2D
        std::vector<Vec2> pts(n);
        for (int i = 0; i < n; i++) {
            pts[i] = project(getPos(face[i]));
        }

        // 2D 几何工具
        auto sub2 = [](Vec2 a, Vec2 b) -> Vec2 { return {a.x - b.x, a.y - b.y}; };
        auto cross2 = [](Vec2 a, Vec2 b) -> float { return a.x * b.y - a.y * b.x; };
        auto dot2   = [](Vec2 a, Vec2 b) -> float { return a.x * b.x + a.y * b.y; };

        // 判断多边形旋向（用于区分凸/凹顶点）
        float signedArea = 0.0f;
        for (int i = 0; i < n; i++) {
            signedArea += cross2(pts[i], pts[(i + 1) % n]);
        }
        bool isCCW = signedArea > 0.0f;

        // 点 p 是否严格在三角形 (a,b,c) 内部（重心坐标法）
        auto pointInTriangle = [&](Vec2 p, Vec2 a, Vec2 b, Vec2 c) -> bool {
            Vec2 v0 = sub2(c, a);
            Vec2 v1 = sub2(b, a);
            Vec2 v2 = sub2(p, a);
            float dot00 = dot2(v0, v0);
            float dot01 = dot2(v0, v1);
            float dot02 = dot2(v0, v2);
            float dot11 = dot2(v1, v1);
            float dot12 = dot2(v1, v2);
            float invD = 1.0f / (dot00 * dot11 - dot01 * dot01);
            float u = (dot11 * dot02 - dot01 * dot12) * invD;
            float v = (dot00 * dot12 - dot01 * dot02) * invD;
            return u > 0.0f && v > 0.0f && u + v < 1.0f;
        };

        // 耳切法主循环
        std::vector<int> working(n);
        for (int i = 0; i < n; i++) working[i] = i;

        while (working.size() > 3) {
            int m = (int)working.size();
            bool earFound = false;

            for (int wi = 0; wi < m && !earFound; wi++) {
                int pi = (wi - 1 + m) % m;
                int ni = (wi + 1) % m;

                int prevIdx = working[pi];
                int currIdx = working[wi];
                int nextIdx = working[ni];

                Vec2 a = pts[prevIdx];
                Vec2 b = pts[currIdx];
                Vec2 c = pts[nextIdx];

                //  必须是凸顶点（拐弯方向与多边形一致）
                float crossVal = cross2(sub2(b, a), sub2(c, b));
                bool convex = isCCW ? (crossVal > 1e-6f) : (crossVal < -1e-6f);
                if (!convex) continue;

                //  三角形 (a,b,c) 内不能有其他顶点
                bool hasInside = false;
                for (int j = 0; j < m && !hasInside; j++) {
                    if (j == wi || j == pi || j == ni) continue;
                    if (pointInTriangle(pts[working[j]], a, b, c))
                        hasInside = true;
                }
                if (hasInside) continue;

                indices.push_back(static_cast<uint32_t>(face[prevIdx] - 1));
                indices.push_back(static_cast<uint32_t>(face[currIdx] - 1));
                indices.push_back(static_cast<uint32_t>(face[nextIdx] - 1));

                working.erase(working.begin() + wi);
                earFound = true;
            }

            // 防御：数值不稳定时回退到扇形剖分
            if (!earFound) {
                for (size_t i = 1; i + 1 < working.size(); i++) {
                    indices.push_back(static_cast<uint32_t>(face[working[0]] - 1));
                    indices.push_back(static_cast<uint32_t>(face[working[i]] - 1));
                    indices.push_back(static_cast<uint32_t>(face[working[i + 1]] - 1));
                }
                break;
            }
        }

        if (working.size() == 3) {
            indices.push_back(static_cast<uint32_t>(face[working[0]] - 1));
            indices.push_back(static_cast<uint32_t>(face[working[1]] - 1));
            indices.push_back(static_cast<uint32_t>(face[working[2]] - 1));
        }
    };

    // --- 2c. 主解析循环 ---
    Vec3 bboxMin = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    Vec3 bboxMax = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };

    while (ptr < end) {
        if (ptr[0] == 'v' && ptr[1] == ' ') {
            ptr += 2;
            float x = parseFloat();
            float y = parseFloat();
            float z = parseFloat();

            Vertex3D vert{};
            vert.position[0] = x;
            vert.position[1] = y;
            vert.position[2] = z;
            vert.color[0]    = 1.0f;
            vert.color[1]    = 1.0f;
            vert.color[2]    = 1.0f;
            vert.texCoord[0] = 0.0f;
            vert.texCoord[1] = 0.0f;
            vertices.push_back(vert);

            bboxMin.x = std::min(bboxMin.x, x);
            bboxMin.y = std::min(bboxMin.y, y);
            bboxMin.z = std::min(bboxMin.z, z);
            bboxMax.x = std::max(bboxMax.x, x);
            bboxMax.y = std::max(bboxMax.y, y);
            bboxMax.z = std::max(bboxMax.z, z);
        } else if (ptr[0] == 'f' && ptr[1] == ' ') {
            ptr += 2;

            // 读取面上所有顶点索引（数量可变，支持四边形等多边形）
            std::vector<int> faceIndices;
            faceIndices.reserve(16);

            while (ptr < end && *ptr != '\n' && *ptr != '\r' && *ptr != '#') {
                skipWhitespace();
                if (ptr >= end) break;
                if (*ptr == '\n' || *ptr == '\r' || *ptr == '#') break;
                if (*ptr != '-' && (*ptr < '0' || *ptr > '9')) break;

                int idx = parseFaceIndex();
                faceIndices.push_back(idx);
            }

            if (faceIndices.size() >= 3) {
                triangulateFace(faceIndices);
            }
        }

        // 跳到下一行（兼容 Windows \r\n 和 Unix \n）
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr;
    }

    // --- 3. 将模型缩放到 [-1, 1] 范围并居中 ---
    Vec3 center = { (bboxMin.x + bboxMax.x) * 0.5f,
                    (bboxMin.y + bboxMax.y) * 0.5f,
                    (bboxMin.z + bboxMax.z) * 0.5f };
    Vec3 size = { bboxMax.x - bboxMin.x,
                  bboxMax.y - bboxMin.y,
                  bboxMax.z - bboxMin.z };
    float scale = 2.0f / std::max({ size.x, size.y, size.z });
    for (auto& vert : vertices) {
        vert.position[0] = (vert.position[0] - center.x) * scale;
        vert.position[1] = (vert.position[1] - center.y) * scale;
        vert.position[2] = (vert.position[2] - center.z) * scale;

        float t = vert.position[2] * 0.5f + 0.5f;
        float r, g, b;
        if (t < 0.5f) {
            float u = t / 0.5f;
            r = 0.0f + u * 0.6f;
            g = 0.5f + u * (0.3f - 0.5f);
            b = 0.05f + u * 0.05f;
        } else {
            float u = (t - 0.5f) / 0.5f;
            r = 0.6f + u * 0.3f;
            g = 0.3f + u * 0.6f;
            b = 0.1f + u * 0.8f;
        }
        vert.color[0] = r;
        vert.color[1] = g;
        vert.color[2] = b;
    }

    // --- 4. 回调主线程 ---
    auto sharedVerts = std::make_shared<std::vector<Vertex3D>>(std::move(vertices));
    auto sharedIdxs  = std::make_shared<std::vector<uint32_t>>(std::move(indices));
    auto callback    = m_callback;

    QMetaObject::invokeMethod(QApplication::instance(),
        [callback, sharedVerts, sharedIdxs, center, scale]() {
            callback(std::move(*sharedVerts), std::move(*sharedIdxs), center, scale);
        }, Qt::QueuedConnection);
}
