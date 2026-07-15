#include "ObjParseRunnable.h"
#include <QApplication>
#include <QMetaObject>
#include <fstream>
#include <cstring>
#include <cstdlib>
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

    // --- 2. 第一遍扫描：统计 v 和 f 的行数（预分配优化） ---
    size_t vertexCount = 0;
    size_t faceCount = 0;

    {
        const char* ptr = content.data();
        const char* end = ptr + content.size();
        while (ptr < end) {
            if (ptr[0] == 'v' && ptr[1] == ' ') {
                ++vertexCount;
            } else if (ptr[0] == 'f' && ptr[1] == ' ') {
                ++faceCount;
            }
            // 跳到下一行
            while (ptr < end && *ptr != '\n') ++ptr;
            if (ptr < end) ++ptr; // skip '\n'
        }
    }

    // --- 3. 预分配容器 ---
    std::vector<VulkanVertex> vertices;
    vertices.reserve(vertexCount);
    std::vector<uint32_t> indices;
    indices.reserve(faceCount * 3);

    // --- 4. 第二遍：解析 ---
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

    while (ptr < end) {
        if (ptr[0] == 'v' && ptr[1] == ' ') {
            ptr += 2; // skip "v "
            float x = parseFloat();
            float y = parseFloat();
            float z = parseFloat();

            VulkanVertex vert{};
            vert.position = { x, y, z };
            vert.color    = { 1.0f, 1.0f, 1.0f }; // 默认白色
            vert.texCoord = { 0.0f, 0.0f };        // 默认纹理坐标
            vertices.push_back(vert);
        } else if (ptr[0] == 'f' && ptr[1] == ' ') {
            ptr += 2; // skip "f "
            int i1 = parseInt();
            int i2 = parseInt();
            int i3 = parseInt();

            // OBJ 索引从 1 开始，转为 0-based
            indices.push_back(static_cast<uint32_t>(i1 - 1));
            indices.push_back(static_cast<uint32_t>(i2 - 1));
            indices.push_back(static_cast<uint32_t>(i3 - 1));
        }

        // 跳到下一行
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr; // skip '\n'
    }

    // --- 5. 回调主线程（使 lambda 可拷贝：将数据移入 shared_ptr） ---
    auto sharedVerts = std::make_shared<std::vector<VulkanVertex>>(std::move(vertices));
    auto sharedIdxs  = std::make_shared<std::vector<uint32_t>>(std::move(indices));
    auto callback    = m_callback;

    QMetaObject::invokeMethod(QApplication::instance(),
        [callback, sharedVerts, sharedIdxs]() {
            callback(std::move(*sharedVerts), std::move(*sharedIdxs));
        }, Qt::QueuedConnection);
}
