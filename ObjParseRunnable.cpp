#include "ObjParseRunnable.h"
#include <QApplication>
#include <QMetaObject>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <memory>

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

    // --- 2. 单遍解析（不预扫描，vector 动态增长即可） ---
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // 根据文件大小粗略预分配，减少 reallocation
    vertices.reserve(fileSize / 64);
    indices.reserve(fileSize / 64);

    // --- 3. 解析 ---
    const char* ptr = content.data();
    const char* end = ptr + content.size();

    // 辅助：跳过空白
    auto skipWhitespace = [&ptr, end]() {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
    };

    // 解析 float（按 strtod 语义跳过前导空白）
    auto parseFloat = [&ptr, end]() -> float {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
        char* endPtr = nullptr;
        float val = static_cast<float>(std::strtod(ptr, &endPtr));
        ptr = endPtr;
        return val;
    };

    // 解析 int
    auto parseInt = [&ptr, end]() -> int {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
        char* endPtr = nullptr;
        int val = static_cast<int>(std::strtol(ptr, &endPtr, 10));
        ptr = endPtr;
        return val;
    };

    // 解析 face 中的单个顶点索引（支持 v、v/vt、v/vt/vn、v//vn、v/ 等格式）
    auto parseFaceIndex = [&ptr, end, &parseInt]() -> int {
        int val = parseInt();
        // 跳过可选的 /vt 和 /vn 部分
        if (ptr < end && *ptr == '/') {
            ++ptr;
            if (ptr < end && (*ptr == '-' || (*ptr >= '0' && *ptr <= '9'))) {
                char* endPtr = nullptr;
                std::strtol(ptr, &endPtr, 10);
                ptr = endPtr;
            }
            if (ptr < end && *ptr == '/') {
                ++ptr;
                if (ptr < end && (*ptr == '-' || (*ptr >= '0' && *ptr <= '9'))) {
                    char* endPtr = nullptr;
                    std::strtol(ptr, &endPtr, 10);
                    ptr = endPtr;
                }
            }
        }
        return val;
    };

    // 包围盒（用于后续缩放到 [-1, 1] 范围并居中）
    Vec3 bboxMin = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    Vec3 bboxMax = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };

    while (ptr < end) {
        if (ptr[0] == 'v' && ptr[1] == ' ') {
            ptr += 2; // skip "v "
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
            ptr += 2; // skip "f "
            int i1 = parseFaceIndex();
            int i2 = parseFaceIndex();
            int i3 = parseFaceIndex();

            // OBJ 索引从 1 开始，转为 0-based
            indices.push_back(static_cast<uint32_t>(i1 - 1));
            indices.push_back(static_cast<uint32_t>(i2 - 1));
            indices.push_back(static_cast<uint32_t>(i3 - 1));
        }

        // 跳到下一行
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr; // skip '\n'
    }

    // --- 4. 将模型缩放到 [-1, 1] 范围并居中 ---
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

    // --- 5. 回调主线程（使 lambda 可拷贝：将数据移入 shared_ptr） ---
    auto sharedVerts = std::make_shared<std::vector<Vertex3D>>(std::move(vertices));
    auto sharedIdxs  = std::make_shared<std::vector<uint32_t>>(std::move(indices));
    auto callback    = m_callback;

    QMetaObject::invokeMethod(QApplication::instance(),
        [callback, sharedVerts, sharedIdxs]() {
            callback(std::move(*sharedVerts), std::move(*sharedIdxs));
        }, Qt::QueuedConnection);
}
