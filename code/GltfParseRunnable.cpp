#include "GltfParseRunnable.h"

#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr int kComponentByte = 5120;
constexpr int kComponentUnsignedByte = 5121;
constexpr int kComponentShort = 5122;
constexpr int kComponentUnsignedShort = 5123;
constexpr int kComponentUnsignedInt = 5125;
constexpr int kComponentFloat = 5126;

constexpr uint32_t kGlbMagic = 0x46546C67u;      // "glTF"
constexpr uint32_t kGlbChunkJson = 0x4E4F534Au;  // "JSON"
constexpr uint32_t kGlbChunkBin = 0x004E4942u;   // "BIN\0"

// 读取整个文件；成功时 out 为原始字节。
bool ReadWholeFileBytes(const std::filesystem::path& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(fileSize));
    return static_cast<bool>(file.read(out.data(), fileSize));
}

uint32_t ReadU32(const char* data) {
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

// 判断是否为 GLB 容器。
bool IsGlb(const std::vector<char>& bytes) {
    return bytes.size() >= 4 && ReadU32(bytes.data()) == kGlbMagic;
}

// 组件字节数；不支持时返回 0。
int ComponentSize(int componentType) {
    switch (componentType) {
    case kComponentByte:
    case kComponentUnsignedByte:
        return 1;
    case kComponentShort:
    case kComponentUnsignedShort:
        return 2;
    case kComponentUnsignedInt:
    case kComponentFloat:
        return 4;
    default:
        return 0;
    }
}

// 类型分量数；不支持时返回 0。
int TypeComponentCount(const QString& type) {
    if (type == QLatin1String("SCALAR")) return 1;
    if (type == QLatin1String("VEC2")) return 2;
    if (type == QLatin1String("VEC3")) return 3;
    if (type == QLatin1String("VEC4")) return 4;
    if (type == QLatin1String("MAT4")) return 16;
    return 0;
}

int IntField(const QJsonObject& obj, const char* key, int fallback) {
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isDouble() ? value.toInt() : fallback;
}

double DoubleField(const QJsonObject& obj, const char* key, double fallback) {
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

std::string StringField(const QJsonObject& obj, const char* key) {
    return obj.value(QLatin1String(key)).toString().toStdString();
}

// 解析 base64 文本（允许含换行与空白）。
bool DecodeBase64(const std::string& text, std::vector<char>& out) {
    auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    out.clear();
    int accumulator = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int value = valueOf(c);
        if (value < 0) continue;  // 跳过换行等空白
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return true;
}

// 解析 data URI；非 base64 或不支持时返回 false。
bool DecodeDataUri(const std::string& uri, std::vector<char>& out) {
    const std::string marker = ";base64,";
    const size_t position = uri.find(marker);
    if (position == std::string::npos) return false;
    return DecodeBase64(uri.substr(position + marker.size()), out);
}

// 读取一个分量并转为 float（含归一化整数）。
float ReadComponentAsFloat(const char* element, int componentType, bool normalized, int component) {
    switch (componentType) {
    case kComponentFloat: {
        float value = 0.0f;
        std::memcpy(&value, element + component * 4, sizeof(value));
        return value;
    }
    case kComponentUnsignedByte: {
        const uint8_t value = static_cast<uint8_t>(element[component]);
        return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
    }
    case kComponentByte: {
        const int8_t value = static_cast<int8_t>(element[component]);
        return normalized
            ? std::max(static_cast<float>(value) / 127.0f, -1.0f)
            : static_cast<float>(value);
    }
    case kComponentUnsignedShort: {
        uint16_t value = 0;
        std::memcpy(&value, element + component * 2, sizeof(value));
        return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
    }
    case kComponentShort: {
        int16_t value = 0;
        std::memcpy(&value, element + component * 2, sizeof(value));
        return normalized
            ? std::max(static_cast<float>(value) / 32767.0f, -1.0f)
            : static_cast<float>(value);
    }
    default:
        return 0.0f;
    }
}

// 读取一个索引分量。
uint32_t ReadComponentAsIndex(const char* element, int componentType) {
    switch (componentType) {
    case kComponentUnsignedByte:
        return static_cast<uint32_t>(static_cast<uint8_t>(*element));
    case kComponentUnsignedShort: {
        uint16_t value = 0;
        std::memcpy(&value, element, sizeof(value));
        return static_cast<uint32_t>(value);
    }
    case kComponentUnsignedInt: {
        uint32_t value = 0;
        std::memcpy(&value, element, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

// 解析后的访问器视图。
struct AccessorInfo {
    bool valid = false;
    int componentType = 0;
    int componentCount = 0;
    size_t count = 0;
    bool normalized = false;
    const char* base = nullptr;  // 元素 0 起点
    size_t stride = 0;           // 元素字节步长
};

// 解析 accessor 到可读视图。
bool ResolveAccessor(const QJsonArray& accessors,
                     const QJsonArray& bufferViews,
                     const std::vector<std::vector<char>>& buffers,
                     int accessorIndex,
                     AccessorInfo& out,
                     std::string& error) {
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        error = "accessor 索引越界";
        return false;
    }
    const QJsonObject accessor = accessors.at(accessorIndex).toObject();

    if (accessor.contains(QLatin1String("sparse"))) {
        error = "不支持 sparse accessor";
        return false;
    }

    const int componentType = IntField(accessor, "componentType", 0);
    const int componentSize = ComponentSize(componentType);
    const int componentCount = TypeComponentCount(accessor.value(QLatin1String("type")).toString());
    if (componentSize == 0 || componentCount == 0) {
        error = "不支持的 accessor 组件类型或分量数";
        return false;
    }

    const int viewIndex = IntField(accessor, "bufferView", -1);
    if (viewIndex < 0 || viewIndex >= bufferViews.size()) {
        error = "accessor 缺少有效的 bufferView";
        return false;
    }
    const QJsonObject view = bufferViews.at(viewIndex).toObject();

    const int bufferIndex = IntField(view, "buffer", -1);
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) {
        error = "bufferView 指向无效 buffer";
        return false;
    }

    const size_t viewOffset = static_cast<size_t>(DoubleField(view, "byteOffset", 0.0));
    const size_t accessorOffset = static_cast<size_t>(DoubleField(accessor, "byteOffset", 0.0));
    const size_t packedStride = static_cast<size_t>(componentSize * componentCount);
    const int declaredStride = IntField(view, "byteStride", 0);
    const size_t stride = declaredStride > 0 ? static_cast<size_t>(declaredStride) : packedStride;

    const std::vector<char>& buffer = buffers[static_cast<size_t>(bufferIndex)];
    const size_t elementOffset = viewOffset + accessorOffset;
    const size_t count = static_cast<size_t>(DoubleField(accessor, "count", 0.0));
    const size_t requiredEnd = count == 0
        ? elementOffset
        : elementOffset + (count - 1) * stride + packedStride;
    if (requiredEnd > buffer.size()) {
        error = "accessor 数据超出 buffer 范围";
        return false;
    }

    out.valid = true;
    out.componentType = componentType;
    out.componentCount = componentCount;
    out.count = count;
    out.normalized = accessor.value(QLatin1String("normalized")).toBool(false);
    out.base = buffer.data() + elementOffset;
    out.stride = stride;
    return true;
}

const char* AccessorElement(const AccessorInfo& info, size_t element) {
    return info.base + element * info.stride;
}

// 节点局部变换（matrix 优先，否则 TRS）。
glm::mat4 NodeLocalMatrix(const QJsonObject& node) {
    if (node.contains(QLatin1String("matrix"))) {
        const QJsonArray values = node.value(QLatin1String("matrix")).toArray();
        if (values.size() == 16) {
            float matrix[16];
            for (int i = 0; i < 16; ++i) {
                matrix[i] = static_cast<float>(values.at(i).toDouble());
            }
            return glm::make_mat4(matrix);
        }
    }

    glm::mat4 result(1.0f);
    const QJsonArray translation = node.value(QLatin1String("translation")).toArray();
    if (translation.size() == 3) {
        result = glm::translate(result, glm::vec3(
            static_cast<float>(translation.at(0).toDouble()),
            static_cast<float>(translation.at(1).toDouble()),
            static_cast<float>(translation.at(2).toDouble())));
    }
    const QJsonArray rotation = node.value(QLatin1String("rotation")).toArray();
    if (rotation.size() == 4) {
        const glm::quat quaternion(
            static_cast<float>(rotation.at(3).toDouble()),  // w
            static_cast<float>(rotation.at(0).toDouble()),  // x
            static_cast<float>(rotation.at(1).toDouble()),  // y
            static_cast<float>(rotation.at(2).toDouble())); // z
        result *= glm::mat4_cast(quaternion);
    }
    const QJsonArray scale = node.value(QLatin1String("scale")).toArray();
    if (scale.size() == 3) {
        result = glm::scale(result, glm::vec3(
            static_cast<float>(scale.at(0).toDouble()),
            static_cast<float>(scale.at(1).toDouble()),
            static_cast<float>(scale.at(2).toDouble())));
    }
    return result;
}

void ExpandBounds(Aabb& bounds, const Vec3& point) {
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

Aabb EmptyBounds() {
    Aabb bounds;
    bounds.min = { std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max() };
    bounds.max = { std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest() };
    return bounds;
}

} // 匿名命名空间

GltfParseRunnable::GltfParseRunnable(std::string filePath, Callback callback,
                                     DiagnosticCallback diagnosticCallback)
    : m_filePath(std::move(filePath))
    , m_callback(std::move(callback))
    , m_diagnosticCallback(std::move(diagnosticCallback)) {
    setAutoDelete(true);
}

GltfParseRunnable::~GltfParseRunnable() = default;

// 执行后台任务。
void GltfParseRunnable::run() {
    auto report = [diagnostic = m_diagnosticCallback](std::string message, bool isError) {
        if (!diagnostic) return;
        QMetaObject::invokeMethod(QApplication::instance(),
            [diagnostic, message = std::move(message), isError]() {
                diagnostic(message, isError);
            }, Qt::QueuedConnection);
    };

    try {
        const std::filesystem::path path = std::filesystem::u8path(m_filePath);
        const std::string fileName = path.filename().u8string();

        std::vector<char> fileBytes;
        if (!ReadWholeFileBytes(path, fileBytes)) {
            throw std::runtime_error("无法读取 glTF 文件: " + m_filePath);
        }

        SceneAsset asset;
        asset.sourcePath = m_filePath;

        auto addMessage = [&](std::string text, bool isError) {
            asset.messages.push_back(ImportMessage{ text, isError });
            report(text, isError);
        };
        auto addWarning = [&](const std::string& reason) {
            addMessage("[警告] " + fileName + ": " + reason, false);
        };

        // 1. 容器：GLB 或纯 JSON。
        std::string jsonText;
        std::vector<char> binChunk;
        if (IsGlb(fileBytes)) {
            size_t offset = 12;  // 跳过 12 字节头
            while (offset + 8 <= fileBytes.size()) {
                const uint32_t chunkLength = ReadU32(fileBytes.data() + offset);
                const uint32_t chunkType = ReadU32(fileBytes.data() + offset + 4);
                offset += 8;
                if (offset + chunkLength > fileBytes.size()) break;
                if (chunkType == kGlbChunkJson) {
                    jsonText.assign(fileBytes.data() + offset, chunkLength);
                } else if (chunkType == kGlbChunkBin) {
                    binChunk.assign(fileBytes.data() + offset, fileBytes.data() + offset + chunkLength);
                }
                offset += chunkLength;
            }
            if (jsonText.empty()) {
                throw std::runtime_error("GLB 缺少 JSON 块: " + m_filePath);
            }
        } else {
            jsonText.assign(fileBytes.begin(), fileBytes.end());
        }

        QJsonParseError parseError{};
        const QJsonDocument document =
            QJsonDocument::fromJson(QByteArray(jsonText.data(), static_cast<int>(jsonText.size())), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            throw std::runtime_error("glTF JSON 解析失败: " + parseError.errorString().toStdString());
        }
        if (!document.isObject()) {
            throw std::runtime_error("glTF 根节点不是 JSON 对象: " + m_filePath);
        }
        const QJsonObject root = document.object();
        const QJsonArray bufferViews = root.value(QLatin1String("bufferViews")).toArray();
        const QJsonArray accessors = root.value(QLatin1String("accessors")).toArray();

        // 2. buffers：GLB 二进制块、data URI 或外部相对路径。
        const QJsonArray jsonBuffers = root.value(QLatin1String("buffers")).toArray();
        std::vector<std::vector<char>> buffers(jsonBuffers.size());
        for (int i = 0; i < jsonBuffers.size(); ++i) {
            const QJsonObject buffer = jsonBuffers.at(i).toObject();
            const std::string uri = StringField(buffer, "uri");
            if (uri.empty()) {
                buffers[i] = binChunk;  // GLB 的 buffer 0
                continue;
            }
            if (uri.rfind("data:", 0) == 0) {
                if (!DecodeDataUri(uri, buffers[i])) {
                    addWarning("不支持非 base64 的 data URI buffer");
                }
                continue;
            }
            const std::filesystem::path external =
                path.parent_path() / std::filesystem::u8path(uri);
            if (!ReadWholeFileBytes(external, buffers[i])) {
                addWarning("无法读取外部 buffer: " + uri);
            }
        }

        // 3. 采样器 / 图像 / 纹理声明（1.2 只记录来源，不解码像素）。
        const QJsonArray jsonSamplers = root.value(QLatin1String("samplers")).toArray();
        for (const QJsonValue& value : jsonSamplers) {
            const QJsonObject sampler = value.toObject();
            Sampler entry;
            entry.name = StringField(sampler, "name");
            entry.wrapS = IntField(sampler, "wrapS", 10497);
            entry.wrapT = IntField(sampler, "wrapT", 10497);
            entry.minFilter = IntField(sampler, "minFilter", 0);
            entry.magFilter = IntField(sampler, "magFilter", 0);
            asset.samplers.push_back(std::move(entry));
        }

        const QJsonArray jsonImages = root.value(QLatin1String("images")).toArray();
        for (const QJsonValue& value : jsonImages) {
            const QJsonObject image = value.toObject();
            Image entry;
            entry.name = StringField(image, "name");
            entry.uri = StringField(image, "uri");
            entry.bufferView = IntField(image, "bufferView", -1);
            entry.mimeType = StringField(image, "mimeType");

            // 1.3：解析编码字节，供上层解码后上传为 GPU 纹理。
            if (!entry.uri.empty()) {
                if (entry.uri.rfind("data:", 0) == 0) {
                    std::vector<char> decoded;
                    if (DecodeDataUri(entry.uri, decoded)) {
                        entry.encodedData.assign(decoded.begin(), decoded.end());
                    } else {
                        addWarning("不支持非 base64 的图像 data URI");
                    }
                } else {
                    const std::filesystem::path external =
                        path.parent_path() / std::filesystem::u8path(entry.uri);
                    std::vector<char> decoded;
                    if (ReadWholeFileBytes(external, decoded)) {
                        entry.encodedData.assign(decoded.begin(), decoded.end());
                        entry.resolvedPath = external.u8string();
                    } else {
                        addWarning("无法读取图像文件: " + entry.uri);
                    }
                }
            } else if (entry.bufferView >= 0 && entry.bufferView < bufferViews.size()) {
                const QJsonObject view = bufferViews.at(entry.bufferView).toObject();
                const int bufferIndex = IntField(view, "buffer", -1);
                const size_t viewOffset =
                    static_cast<size_t>(DoubleField(view, "byteOffset", 0.0));
                const size_t viewLength =
                    static_cast<size_t>(DoubleField(view, "byteLength", 0.0));
                if (bufferIndex >= 0 && bufferIndex < static_cast<int>(buffers.size()) &&
                    viewOffset + viewLength <= buffers[static_cast<size_t>(bufferIndex)].size()) {
                    const std::vector<char>& buffer = buffers[static_cast<size_t>(bufferIndex)];
                    const auto begin = buffer.begin() + static_cast<std::ptrdiff_t>(viewOffset);
                    entry.encodedData.assign(begin, begin + static_cast<std::ptrdiff_t>(viewLength));
                } else {
                    addWarning("图像 bufferView 指向无效数据");
                }
            } else {
                addWarning("图像既无 uri 也无有效 bufferView");
            }

            asset.images.push_back(std::move(entry));
        }

        const QJsonArray jsonTextures = root.value(QLatin1String("textures")).toArray();
        for (const QJsonValue& value : jsonTextures) {
            const QJsonObject texture = value.toObject();
            Texture entry;
            entry.name = StringField(texture, "name");
            entry.image = IntField(texture, "source", -1);
            entry.sampler = IntField(texture, "sampler", -1);
            asset.textures.push_back(std::move(entry));
        }

        // 4. 材质：基础色 + 基础色纹理。
        const QJsonArray jsonMaterials = root.value(QLatin1String("materials")).toArray();
        for (const QJsonValue& value : jsonMaterials) {
            const QJsonObject material = value.toObject();
            Material entry;
            entry.name = StringField(material, "name");
            const QJsonObject pbr =
                material.value(QLatin1String("pbrMetallicRoughness")).toObject();
            const QJsonArray baseColor = pbr.value(QLatin1String("baseColorFactor")).toArray();
            if (baseColor.size() == 4) {
                entry.baseColor = {
                    static_cast<float>(baseColor.at(0).toDouble()),
                    static_cast<float>(baseColor.at(1).toDouble()),
                    static_cast<float>(baseColor.at(2).toDouble()),
                    static_cast<float>(baseColor.at(3).toDouble())
                };
            }
            const QJsonObject baseColorTexture =
                pbr.value(QLatin1String("baseColorTexture")).toObject();
            entry.baseColorTexture = IntField(baseColorTexture, "index", -1);

            // 透明度分类（1.4）：alphaMode 决定 Opaque/Mask/Blend，alphaCutoff 供 Mask 使用。
            const std::string alphaMode = StringField(material, "alphaMode");
            if (alphaMode == "MASK") {
                entry.alphaMode = MaterialAlphaMode::Mask;
            } else if (alphaMode == "BLEND") {
                entry.alphaMode = MaterialAlphaMode::Blend;
            } else {
                entry.alphaMode = MaterialAlphaMode::Opaque;
            }
            if (material.contains(QLatin1String("alphaCutoff"))) {
                entry.alphaCutoff = static_cast<float>(
                    material.value(QLatin1String("alphaCutoff")).toDouble(0.5));
            }

            asset.materials.push_back(std::move(entry));
        }

        // 5. 节点：层级与局部变换；再算世界变换。
        const QJsonArray jsonNodes = root.value(QLatin1String("nodes")).toArray();
        asset.nodes.resize(jsonNodes.size());
        std::vector<glm::mat4> localMatrices(jsonNodes.size(), glm::mat4(1.0f));
        for (int i = 0; i < jsonNodes.size(); ++i) {
            const QJsonObject node = jsonNodes.at(i).toObject();
            SceneNode& entry = asset.nodes[i];
            entry.name = StringField(node, "name");
            entry.mesh = IntField(node, "mesh", -1);
            entry.localTransform = Mat4{};
            localMatrices[i] = NodeLocalMatrix(node);
            std::memcpy(entry.localTransform.m, glm::value_ptr(localMatrices[i]),
                        sizeof(float) * 16);
            for (const QJsonValue& child : node.value(QLatin1String("children")).toArray()) {
                const int childIndex = child.toInt(-1);
                if (childIndex >= 0 && childIndex < jsonNodes.size()) {
                    entry.children.push_back(childIndex);
                    asset.nodes[childIndex].parent = i;
                }
            }
        }

        // 根节点：优先 scene，否则取无父节点者。
        std::vector<int> roots;
        const QJsonArray scenes = root.value(QLatin1String("scenes")).toArray();
        const int sceneIndex = IntField(root, "scene", -1);
        if (sceneIndex >= 0 && sceneIndex < scenes.size()) {
            for (const QJsonValue& node : scenes.at(sceneIndex).toObject()
                     .value(QLatin1String("nodes")).toArray()) {
                const int nodeIndex = node.toInt(-1);
                if (nodeIndex >= 0 && nodeIndex < jsonNodes.size()) roots.push_back(nodeIndex);
            }
        }
        if (roots.empty()) {
            for (int i = 0; i < static_cast<int>(asset.nodes.size()); ++i) {
                if (asset.nodes[i].parent < 0) roots.push_back(i);
            }
        }

        std::vector<glm::mat4> worldMatrices(jsonNodes.size(), glm::mat4(1.0f));
        std::vector<int> traversalOrder;
        {
            std::vector<int> stack(roots.rbegin(), roots.rend());
            while (!stack.empty()) {
                const int index = stack.back();
                stack.pop_back();
                const int parent = asset.nodes[index].parent;
                worldMatrices[index] = (parent >= 0 && parent < jsonNodes.size())
                    ? worldMatrices[parent] * localMatrices[index]
                    : localMatrices[index];
                traversalOrder.push_back(index);
                const std::vector<int>& children = asset.nodes[index].children;
                for (auto it = children.rbegin(); it != children.rend(); ++it) {
                    stack.push_back(*it);
                }
            }
        }

        // 6. 网格：每个带 mesh 的节点产出一个 MeshData，世界变换烘焙进顶点。
        const QJsonArray jsonMeshes = root.value(QLatin1String("meshes")).toArray();
        for (int nodeIndex : traversalOrder) {
            const int meshIndex = asset.nodes[nodeIndex].mesh;
            if (meshIndex < 0 || meshIndex >= jsonMeshes.size()) continue;

            const QJsonObject jsonMesh = jsonMeshes.at(meshIndex).toObject();
            const QJsonArray primitives = jsonMesh.value(QLatin1String("primitives")).toArray();
            const glm::mat4 world = worldMatrices[nodeIndex];
            const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(world));

            MeshData mesh;
            mesh.name = asset.nodes[nodeIndex].name;
            if (mesh.name.empty()) mesh.name = StringField(jsonMesh, "name");
            mesh.bounds = EmptyBounds();
            bool meshHasGeometry = false;

            for (const QJsonValue& primitiveValue : primitives) {
                const QJsonObject primitive = primitiveValue.toObject();
                const int mode = IntField(primitive, "mode", 4);
                if (mode != 4) {
                    addWarning("跳过非三角形图元（mode " + std::to_string(mode) + "）");
                    continue;
                }

                const QJsonObject attributes = primitive.value(QLatin1String("attributes")).toObject();
                const int positionAccessor = IntField(attributes, "POSITION", -1);
                if (positionAccessor < 0) {
                    addWarning("跳过缺少 POSITION 的图元");
                    continue;
                }

                AccessorInfo positionInfo;
                std::string error;
                if (!ResolveAccessor(accessors, bufferViews, buffers, positionAccessor,
                                     positionInfo, error)) {
                    addWarning("跳过图元：" + error);
                    continue;
                }

                auto resolveOptional = [&](const char* attribute, AccessorInfo& info) -> bool {
                    const int accessorIndex = IntField(attributes, attribute, -1);
                    if (accessorIndex < 0) return false;
                    std::string reason;
                    return ResolveAccessor(accessors, bufferViews, buffers, accessorIndex,
                                           info, reason);
                };

                AccessorInfo normalInfo;
                AccessorInfo texCoordInfo;
                AccessorInfo colorInfo;
                const bool hasNormal = resolveOptional("NORMAL", normalInfo);
                const bool hasTexCoord = resolveOptional("TEXCOORD_0", texCoordInfo);
                const bool hasColor = resolveOptional("COLOR_0", colorInfo);

                const size_t vertexCount = positionInfo.count;
                const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

                for (size_t i = 0; i < vertexCount; ++i) {
                    const char* positionElement = AccessorElement(positionInfo, i);
                    const glm::vec4 localPosition(
                        ReadComponentAsFloat(positionElement, positionInfo.componentType, positionInfo.normalized, 0),
                        ReadComponentAsFloat(positionElement, positionInfo.componentType, positionInfo.normalized, 1),
                        ReadComponentAsFloat(positionElement, positionInfo.componentType, positionInfo.normalized, 2),
                        1.0f);
                    const glm::vec4 worldPosition = world * localPosition;

                    AssetVertex vertex{};
                    vertex.position[0] = worldPosition.x;
                    vertex.position[1] = worldPosition.y;
                    vertex.position[2] = worldPosition.z;

                    if (hasNormal) {
                        const char* normalElement = AccessorElement(normalInfo, i);
                        const glm::vec3 localNormal(
                            ReadComponentAsFloat(normalElement, normalInfo.componentType, normalInfo.normalized, 0),
                            ReadComponentAsFloat(normalElement, normalInfo.componentType, normalInfo.normalized, 1),
                            ReadComponentAsFloat(normalElement, normalInfo.componentType, normalInfo.normalized, 2));
                        glm::vec3 worldNormal = normalMatrix * localNormal;
                        const float length = glm::length(worldNormal);
                        worldNormal = length > 1.0e-6f ? worldNormal / length : glm::vec3(0.0f, 0.0f, 1.0f);
                        vertex.normal[0] = worldNormal.x;
                        vertex.normal[1] = worldNormal.y;
                        vertex.normal[2] = worldNormal.z;
                    }

                    if (hasTexCoord) {
                        const char* texCoordElement = AccessorElement(texCoordInfo, i);
                        vertex.texCoord[0] = ReadComponentAsFloat(texCoordElement, texCoordInfo.componentType,
                                                                  texCoordInfo.normalized, 0);
                        vertex.texCoord[1] = ReadComponentAsFloat(texCoordElement, texCoordInfo.componentType,
                                                                  texCoordInfo.normalized, 1);
                    }

                    if (hasColor) {
                        const char* colorElement = AccessorElement(colorInfo, i);
                        vertex.color[0] = ReadComponentAsFloat(colorElement, colorInfo.componentType, colorInfo.normalized, 0);
                        vertex.color[1] = ReadComponentAsFloat(colorElement, colorInfo.componentType, colorInfo.normalized, 1);
                        vertex.color[2] = ReadComponentAsFloat(colorElement, colorInfo.componentType, colorInfo.normalized, 2);
                    }

                    mesh.vertices.push_back(vertex);
                }

                const uint32_t indexOffset = static_cast<uint32_t>(mesh.indices.size());
                const int indexAccessor = IntField(primitive, "indices", -1);
                bool indexOversized = false;
                if (indexAccessor >= 0) {
                    AccessorInfo indexInfo;
                    std::string reason;
                    if (!ResolveAccessor(accessors, bufferViews, buffers, indexAccessor, indexInfo, reason)) {
                        addWarning("跳过图元索引：" + reason);
                    } else {
                        for (size_t i = 0; i < indexInfo.count; ++i) {
                            const uint32_t localIndex =
                                ReadComponentAsIndex(AccessorElement(indexInfo, i), indexInfo.componentType);
                            if (localIndex >= vertexCount) {
                                indexOversized = true;
                                continue;
                            }
                            mesh.indices.push_back(baseIndex + localIndex);
                        }
                    }
                } else {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(vertexCount); ++i) {
                        mesh.indices.push_back(baseIndex + i);
                    }
                }
                if (indexOversized) {
                    addWarning("图元存在越界索引，已跳过这些索引");
                }

                SubMesh subMesh;
                subMesh.indexOffset = indexOffset;
                subMesh.indexCount = static_cast<uint32_t>(mesh.indices.size()) - indexOffset;
                subMesh.materialIndex = IntField(primitive, "material", -1);
                subMesh.localBounds = EmptyBounds();
                for (size_t i = subMesh.indexOffset;
                     i < static_cast<size_t>(subMesh.indexOffset) + subMesh.indexCount; ++i) {
                    const AssetVertex& vertex = mesh.vertices[mesh.indices[i]];
                    ExpandBounds(subMesh.localBounds, Vec3{ vertex.position[0], vertex.position[1], vertex.position[2] });
                    ExpandBounds(mesh.bounds, Vec3{ vertex.position[0], vertex.position[1], vertex.position[2] });
                }
                mesh.subMeshes.push_back(subMesh);
                meshHasGeometry = true;
            }

            if (meshHasGeometry && !mesh.vertices.empty() && !mesh.indices.empty()) {
                asset.meshes.push_back(std::move(mesh));
            }
        }

        if (asset.meshes.empty()) {
            throw std::runtime_error("glTF 中没有可渲染的网格: " + m_filePath);
        }

        auto callback = m_callback;
        auto sharedAsset = std::make_shared<SceneAsset>(std::move(asset));
        QMetaObject::invokeMethod(QApplication::instance(),
            [callback, sharedAsset]() {
                if (callback) {
                    callback(std::move(*sharedAsset));
                }
            }, Qt::QueuedConnection);
    } catch (const std::exception& exception) {
        report(std::string("[错误] ") + std::filesystem::u8path(m_filePath).filename().u8string()
                   + ": " + exception.what(), true);
    } catch (...) {
        report("[错误] 解析 glTF 文件时发生未知错误", true);
    }
}
