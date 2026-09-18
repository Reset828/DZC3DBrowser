#include "ObjParseRunnable.h"

#include <QApplication>
#include <QMetaObject>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

struct TexCoord {
    float u = 0.0f;
    float v = 0.0f;
};

struct FaceVertex {
    int position = -1;
    int texCoord = -1;
    int normal = -1;
};

struct VertexKey {
    int position;
    int texCoord;
    int normal;

    bool operator==(const VertexKey& other) const {
        return position == other.position &&
               texCoord == other.texCoord &&
               normal == other.normal;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const {
        size_t result = std::hash<int>{}(key.position);
        result ^= std::hash<int>{}(key.texCoord) + 0x9e3779b9u +
                  (result << 6) + (result >> 2);
        result ^= std::hash<int>{}(key.normal) + 0x9e3779b9u +
                  (result << 6) + (result >> 2);
        return result;
    }
};

// 判断是否为空格或制表符。
bool IsSpace(char c) {
    return c == ' ' || c == '\t';
}

// 跳过无效或空白数据。
void SkipSpace(const char*& ptr, const char* end) {
    while (ptr < end && IsSpace(*ptr)) {
        ++ptr;
    }
}

// 解析浮点数。
bool ParseFloat(const char*& ptr, const char* end, float& value) {
    SkipSpace(ptr, end);
    if (ptr >= end) return false;

    char* parsedEnd = nullptr;
    const float parsed = std::strtof(ptr, &parsedEnd);
    if (parsedEnd == ptr || parsedEnd > end || !std::isfinite(parsed)) {
        return false;
    }
    ptr = parsedEnd;
    value = parsed;
    return true;
}

// 解析整数。
bool ParseInteger(std::string_view text, int& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

// 解析 OBJ 索引。
bool ResolveIndex(int objIndex, size_t count, int& resolved) {
    if (objIndex > 0) {
        resolved = objIndex - 1;
    } else if (objIndex < 0) {
        const long long candidate = static_cast<long long>(count) + objIndex;
        if (candidate < 0 || candidate >= static_cast<long long>(count)) {
            return false;
        }
        resolved = static_cast<int>(candidate);
    } else {
        return false;
    }
    return resolved >= 0 && static_cast<size_t>(resolved) < count;
}

// 解析 f 行上的 v/vt/vn 索引。
bool ParseFaceVertex(std::string_view token,
                     size_t positionCount,
                     size_t texCoordCount,
                     size_t normalCount,
                     FaceVertex& result) {
    const size_t firstSlash = token.find('/');
    const size_t secondSlash = firstSlash == std::string_view::npos
        ? std::string_view::npos
        : token.find('/', firstSlash + 1);
    if (secondSlash != std::string_view::npos &&
        token.find('/', secondSlash + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view positionText = token.substr(0, firstSlash);
    int objPosition = 0;
    if (!ParseInteger(positionText, objPosition) ||
        !ResolveIndex(objPosition, positionCount, result.position)) {
        return false;
    }

    if (firstSlash == std::string_view::npos) return true;

    const size_t texEnd = secondSlash == std::string_view::npos
        ? token.size() : secondSlash;
    const std::string_view texText = token.substr(firstSlash + 1,
                                                  texEnd - firstSlash - 1);
    if (!texText.empty()) {
        int objTexCoord = 0;
        if (!ParseInteger(texText, objTexCoord) ||
            !ResolveIndex(objTexCoord, texCoordCount, result.texCoord)) {
            return false;
        }
    }

    if (secondSlash != std::string_view::npos) {
        const std::string_view normalText = token.substr(secondSlash + 1);
        if (!normalText.empty()) {
            int objNormal = 0;
            if (!ParseInteger(normalText, objNormal) ||
                !ResolveIndex(objNormal, normalCount, result.normal)) {
                return false;
            }
        }
    }
    return true;
}

// 计算向量差。
Vec3 Subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// 计算向量叉积。
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// 计算向量长度平方。
float LengthSquared(const Vec3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

// 归一化向量并处理退化情况。
Vec3 NormalizeOr(const Vec3& value, const Vec3& fallback) {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= std::numeric_limits<float>::epsilon()) {
        return fallback;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {value.x * inverseLength,
            value.y * inverseLength,
            value.z * inverseLength};
}

// 计算三角面的法线。
Vec3 CalculateFaceNormal(const std::vector<FaceVertex>& face,
                         const std::vector<Vec3>& positions) {
    Vec3 normal{};
    for (size_t i = 0; i < face.size(); ++i) {
        const Vec3& current = positions[face[i].position];
        const Vec3& next = positions[face[(i + 1) % face.size()].position];
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }

    if (LengthSquared(normal) <= std::numeric_limits<float>::epsilon()) {
        const Vec3& first = positions[face[0].position];
        for (size_t i = 1; i + 1 < face.size(); ++i) {
            normal = Cross(Subtract(positions[face[i].position], first),
                           Subtract(positions[face[i + 1].position], first));
            if (LengthSquared(normal) > std::numeric_limits<float>::epsilon()) {
                break;
            }
        }
    }
    return NormalizeOr(normal, {0.0f, 0.0f, 1.0f});
}

// 把多边形面剖成三角形。
std::vector<std::array<size_t, 3>> TriangulateFace(
    const std::vector<FaceVertex>& face,
    const std::vector<Vec3>& positions) {
    std::vector<std::array<size_t, 3>> triangles;
    if (face.size() < 3) return triangles;
    if (face.size() == 3) {
        triangles.push_back({0, 1, 2});
        return triangles;
    }

    const Vec3 normal = CalculateFaceNormal(face, positions);
    int dropAxis = 0;
    const float absX = std::abs(normal.x);
    const float absY = std::abs(normal.y);
    const float absZ = std::abs(normal.z);
    if (absY >= absX && absY >= absZ) dropAxis = 1;
    else if (absZ >= absX && absZ >= absY) dropAxis = 2;

    struct Point2 { float x; float y; };
    std::vector<Point2> projected;
    projected.reserve(face.size());
    for (const FaceVertex& vertex : face) {
        const Vec3& p = positions[vertex.position];
        if (dropAxis == 0) projected.push_back({p.y, p.z});
        else if (dropAxis == 1) projected.push_back({p.x, p.z});
        else projected.push_back({p.x, p.y});
    }

    auto cross2 = [](const Point2& a, const Point2& b, const Point2& c) {
        return (b.x - a.x) * (c.y - a.y) -
               (b.y - a.y) * (c.x - a.x);
    };

    float signedArea = 0.0f;
    for (size_t i = 0; i < projected.size(); ++i) {
        const Point2& a = projected[i];
        const Point2& b = projected[(i + 1) % projected.size()];
        signedArea += a.x * b.y - a.y * b.x;
    }

    auto fanFallback = [&]() {
        triangles.clear();
        for (size_t i = 1; i + 1 < face.size(); ++i) {
            triangles.push_back({0, i, i + 1});
        }
    };
    if (std::abs(signedArea) <= 1e-12f) {
        fanFallback();
        return triangles;
    }
    const bool counterClockwise = signedArea > 0.0f;

    auto pointInTriangle = [&](const Point2& point, const Point2& a,
                               const Point2& b, const Point2& c) {
        const float c0 = cross2(a, b, point);
        const float c1 = cross2(b, c, point);
        const float c2 = cross2(c, a, point);
        constexpr float epsilon = 1e-7f;
        if (counterClockwise) {
            return c0 >= -epsilon && c1 >= -epsilon && c2 >= -epsilon;
        }
        return c0 <= epsilon && c1 <= epsilon && c2 <= epsilon;
    };

    std::vector<size_t> remaining(face.size());
    for (size_t i = 0; i < remaining.size(); ++i) remaining[i] = i;

    while (remaining.size() > 3) {
        bool foundEar = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            const size_t previous = remaining[(i + remaining.size() - 1) % remaining.size()];
            const size_t current = remaining[i];
            const size_t next = remaining[(i + 1) % remaining.size()];
            const float turn = cross2(projected[previous], projected[current],
                                      projected[next]);
            if ((counterClockwise && turn <= 1e-7f) ||
                (!counterClockwise && turn >= -1e-7f)) {
                continue;
            }

            bool containsPoint = false;
            for (size_t candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) continue;
                if (pointInTriangle(projected[candidate], projected[previous],
                                    projected[current], projected[next])) {
                    containsPoint = true;
                    break;
                }
            }
            if (containsPoint) continue;

            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            foundEar = true;
            break;
        }
        if (!foundEar) {
            fanFallback();
            return triangles;
        }
    }
    triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return triangles;
}

// 读取整个文件到 out（末尾附加一个 '\0'）；成功时 out.size() = 字节数 + 1。
bool ReadWholeFile(const std::filesystem::path& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) return false;
    file.seekg(0, std::ios::beg);
    out.assign(static_cast<size_t>(fileSize) + 1, '\0');
    if (!file.read(out.data(), fileSize)) return false;
    return true;
}

} // 匿名命名空间

ObjParseRunnable::ObjParseRunnable(std::string filePath, Callback callback,
                                   DiagnosticCallback diagnosticCallback)
    : m_filePath(std::move(filePath))
    , m_callback(std::move(callback))
    , m_diagnosticCallback(std::move(diagnosticCallback)) {
    setAutoDelete(true);
}

ObjParseRunnable::~ObjParseRunnable() = default;

// 执行后台任务。
void ObjParseRunnable::run() {
    auto report = [diagnostic = m_diagnosticCallback](std::string message, bool isError) {
        if (!diagnostic) return;
        QMetaObject::invokeMethod(QApplication::instance(),
            [diagnostic, message = std::move(message), isError]() {
                diagnostic(message, isError);
            }, Qt::QueuedConnection);
    };

    try {
        std::ifstream file(std::filesystem::u8path(m_filePath),
                           std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("无法打开 OBJ 文件: " + m_filePath);
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0) {
            throw std::runtime_error("OBJ 文件为空: " + m_filePath);
        }
        file.seekg(0, std::ios::beg);

        std::vector<char> content(static_cast<size_t>(fileSize) + 1, '\0');
        if (!file.read(content.data(), fileSize)) {
            throw std::runtime_error("读取 OBJ 文件失败: " + m_filePath);
        }

        std::vector<Vec3> positions;
        std::vector<TexCoord> texCoords;
        std::vector<Vec3> normals;
        std::vector<AssetVertex> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexLookup;
        std::vector<uint32_t> positionOnlyLookup;
        // 缺少 OBJ vn 时按位置累加面法线，供线框片元使用稳定的物体空间法线。
        std::vector<Vec3> generatedNormalSums;
        std::vector<uint32_t> generatedVertexPositions;

        // 材质与 SubMesh 划分。
        std::vector<Material> materials;
        materials.push_back(Material{});  // 索引 0：默认白色材质
        std::unordered_map<std::string, int> materialIndexByName;
        std::vector<SubMesh> subMeshes;
        int openSubMeshIndex = -1;
        int currentMaterialIndex = 0;

        // 1.3：MTL 的 map_Kd 纹理声明（图像字节 + 纹理条目）。
        std::vector<Texture> textures;
        std::vector<Image> images;
        std::unordered_map<std::string, int> imageIndexByPath;  // 按解析后路径去重

        std::vector<ImportMessage> messages;

        positions.reserve(static_cast<size_t>(fileSize) / 48);
        vertices.reserve(static_cast<size_t>(fileSize) / 48);
        indices.reserve(static_cast<size_t>(fileSize) / 32);

        Vec3 bboxMin = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
        Vec3 bboxMax = {std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};

        auto addWarning = [&](size_t lineNumber, const char* reason) {
            messages.push_back(ImportMessage{
                "第 " + std::to_string(lineNumber) + " 行: " + reason, false });
        };

        // 结束当前 SubMesh，写入索引数量。
        auto closeOpenSubMesh = [&]() {
            if (openSubMeshIndex < 0) return;
            SubMesh& subMesh = subMeshes[openSubMeshIndex];
            subMesh.indexCount =
                static_cast<uint32_t>(indices.size()) - subMesh.indexOffset;
            openSubMeshIndex = -1;
        };
        // 在当前索引位置开启一个新 SubMesh。
        auto beginSubMesh = [&](int materialIndex) {
            closeOpenSubMesh();
            SubMesh subMesh;
            subMesh.indexOffset = static_cast<uint32_t>(indices.size());
            subMesh.indexCount = 0;
            subMesh.materialIndex = materialIndex;
            subMeshes.push_back(subMesh);
            openSubMeshIndex = static_cast<int>(subMeshes.size()) - 1;
        };
        // 保证当前 SubMesh 使用指定材质。
        auto ensureSubMesh = [&](int materialIndex) {
            if (openSubMeshIndex >= 0 &&
                subMeshes[openSubMeshIndex].materialIndex == materialIndex) {
                return;
            }
            beginSubMesh(materialIndex);
        };

        // 1.3：读取 map_Kd 图像并登记为 Image/Texture；返回纹理索引（失败为 -1）。
        auto registerKdTexture = [&](const std::filesystem::path& imagePath) -> int {
            std::error_code ec;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(imagePath, ec);
            const std::string key = ec ? imagePath.u8string() : canonical.u8string();
            const auto existing = imageIndexByPath.find(key);
            if (existing != imageIndexByPath.end()) {
                for (size_t t = 0; t < textures.size(); ++t) {
                    if (textures[t].image == existing->second) {
                        return static_cast<int>(t);
                    }
                }
            }
            std::vector<char> bytes;
            if (!ReadWholeFile(imagePath, bytes)) {
                return -1;
            }
            if (!bytes.empty() && bytes.back() == '\0') bytes.pop_back();

            Image image;
            image.name = imagePath.filename().u8string();
            image.resolvedPath = key;
            image.encodedData.assign(bytes.begin(), bytes.end());
            images.push_back(std::move(image));
            const int imageIndex = static_cast<int>(images.size()) - 1;
            imageIndexByPath.emplace(key, imageIndex);

            Texture texture;
            texture.name = imagePath.filename().u8string();
            texture.image = imageIndex;
            texture.sampler = -1;
            textures.push_back(std::move(texture));
            return static_cast<int>(textures.size()) - 1;
        };

        // 解析 MTL：newmtl、Kd，以及 map_Kd 基础色纹理。
        auto loadMaterialLibrary = [&](const std::string& libraryName,
                                       size_t lineNumber) {
            const std::filesystem::path objPath =
                std::filesystem::u8path(m_filePath);
            const std::filesystem::path libraryPath =
                objPath.parent_path() / std::filesystem::u8path(libraryName);

            std::vector<char> content;
            if (!ReadWholeFile(libraryPath, content)) {
                addWarning(lineNumber, "无法打开材质库，已跳过");
                return;
            }

            const char* cursor = content.data();
            const char* end = content.data() + (content.size() - 1);
            int current = -1;
            while (cursor < end) {
                const char* lineBegin = cursor;
                while (cursor < end && *cursor != '\n') ++cursor;
                const char* lineEnd = cursor;
                if (lineEnd > lineBegin && lineEnd[-1] == '\r') --lineEnd;
                if (cursor < end) ++cursor;

                const char* ptr = lineBegin;
                SkipSpace(ptr, lineEnd);
                if (ptr >= lineEnd || *ptr == '#') continue;

                const char* keywordBegin = ptr;
                while (ptr < lineEnd && !IsSpace(*ptr)) ++ptr;
                const std::string_view keyword(
                    keywordBegin, static_cast<size_t>(ptr - keywordBegin));

                if (keyword == "newmtl") {
                    SkipSpace(ptr, lineEnd);
                    while (lineEnd > ptr && IsSpace(lineEnd[-1])) --lineEnd;
                    const std::string name(ptr, lineEnd);
                    auto found = materialIndexByName.find(name);
                    if (found != materialIndexByName.end()) {
                        current = found->second;
                    } else {
                        Material material;
                        material.name = name;
                        materials.push_back(material);
                        current = static_cast<int>(materials.size()) - 1;
                        materialIndexByName.emplace(name, current);
                    }
                } else if (keyword == "Kd") {
                    if (current >= 0) {
                        Vec3 rgb{};
                        if (ParseFloat(ptr, lineEnd, rgb.x) &&
                            ParseFloat(ptr, lineEnd, rgb.y) &&
                            ParseFloat(ptr, lineEnd, rgb.z)) {
                            materials[current].baseColor =
                                { rgb.x, rgb.y, rgb.z, 1.0f };
                        }
                    }
                } else if (keyword == "d" || keyword == "Tr") {
                    // d（dissolve）直接给出 alpha；Tr 是其互补（透明度）。
                    if (current >= 0) {
                        float value = 1.0f;
                        if (ParseFloat(ptr, lineEnd, value)) {
                            value = std::clamp(value, 0.0f, 1.0f);
                            if (keyword == "Tr") value = 1.0f - value;
                            materials[current].baseColor.w = value;
                            if (value < 1.0f) {
                                materials[current].alphaMode =
                                    MaterialAlphaMode::Blend;
                            }
                        }
                    }
                } else if (keyword == "map_Kd") {
                    if (current >= 0) {
                        // map_Kd 允许带选项（-o/-s/-bm 等）；取最后一个非选项 token 作为文件名。
                        const char* scan = ptr;
                        const char* fileBegin = nullptr;
                        const char* fileEnd = nullptr;
                        while (scan < lineEnd) {
                            SkipSpace(scan, lineEnd);
                            if (scan >= lineEnd || *scan == '#') break;
                            const char* tokenBegin = scan;
                            while (scan < lineEnd && !IsSpace(*scan) && *scan != '#') ++scan;
                            if (*tokenBegin == '-') {
                                // 跳过该选项的若干参数（简化：跳过一个 token）。
                                continue;
                            }
                            fileBegin = tokenBegin;
                            fileEnd = scan;
                        }
                        if (fileBegin && fileEnd > fileBegin) {
                            std::string imageName(fileBegin,
                                static_cast<size_t>(fileEnd - fileBegin));
                            const std::filesystem::path objPath =
                                std::filesystem::u8path(m_filePath);
                            const std::filesystem::path imagePath =
                                objPath.parent_path() / std::filesystem::u8path(imageName);
                            const int textureIndex = registerKdTexture(imagePath);
                            if (textureIndex >= 0) {
                                materials[current].baseColorTexture = textureIndex;
                            } else {
                                addWarning(lineNumber, "无法读取 map_Kd 纹理，已忽略");
                            }
                        }
                    }
                }
            }
        };

        const char* cursor = content.data();
        const char* contentEnd = content.data() + fileSize;
        size_t lineNumber = 0;
        std::vector<FaceVertex> faceScratch;
        faceScratch.reserve(16);

        while (cursor < contentEnd) {
            ++lineNumber;
            const char* lineBegin = cursor;
            while (cursor < contentEnd && *cursor != '\n') ++cursor;
            const char* lineEnd = cursor;
            if (lineEnd > lineBegin && lineEnd[-1] == '\r') --lineEnd;
            if (cursor < contentEnd) ++cursor;

            const char* ptr = lineBegin;
            if (lineNumber == 1 && lineEnd - ptr >= 3 &&
                static_cast<unsigned char>(ptr[0]) == 0xEF &&
                static_cast<unsigned char>(ptr[1]) == 0xBB &&
                static_cast<unsigned char>(ptr[2]) == 0xBF) {
                ptr += 3;
            }
            SkipSpace(ptr, lineEnd);
            if (ptr >= lineEnd || *ptr == '#') continue;

            const char* keywordBegin = ptr;
            while (ptr < lineEnd && !IsSpace(*ptr)) ++ptr;
            const std::string_view keyword(keywordBegin,
                                           static_cast<size_t>(ptr - keywordBegin));

            if (keyword == "v") {
                Vec3 position{};
                if (!ParseFloat(ptr, lineEnd, position.x) ||
                    !ParseFloat(ptr, lineEnd, position.y) ||
                    !ParseFloat(ptr, lineEnd, position.z)) {
                    addWarning(lineNumber, "顶点坐标无效，已跳过");
                    continue;
                }
                positions.push_back(position);
                bboxMin.x = std::min(bboxMin.x, position.x);
                bboxMin.y = std::min(bboxMin.y, position.y);
                bboxMin.z = std::min(bboxMin.z, position.z);
                bboxMax.x = std::max(bboxMax.x, position.x);
                bboxMax.y = std::max(bboxMax.y, position.y);
                bboxMax.z = std::max(bboxMax.z, position.z);
            } else if (keyword == "vt") {
                TexCoord texCoord{};
                if (!ParseFloat(ptr, lineEnd, texCoord.u)) {
                    addWarning(lineNumber, "纹理坐标无效，已跳过");
                    continue;
                }
                const char* optional = ptr;
                ParseFloat(optional, lineEnd, texCoord.v);
                texCoords.push_back(texCoord);
            } else if (keyword == "vn") {
                Vec3 normal{};
                if (!ParseFloat(ptr, lineEnd, normal.x) ||
                    !ParseFloat(ptr, lineEnd, normal.y) ||
                    !ParseFloat(ptr, lineEnd, normal.z)) {
                    addWarning(lineNumber, "法线无效，已跳过");
                    continue;
                }
                normals.push_back(NormalizeOr(normal, {0.0f, 0.0f, 0.0f}));
            } else if (keyword == "mtllib") {
                SkipSpace(ptr, lineEnd);
                while (ptr < lineEnd) {
                    SkipSpace(ptr, lineEnd);
                    if (ptr >= lineEnd || *ptr == '#') break;
                    const char* tokenBegin = ptr;
                    while (ptr < lineEnd && !IsSpace(*ptr) && *ptr != '#') ++ptr;
                    loadMaterialLibrary(
                        std::string(tokenBegin, static_cast<size_t>(ptr - tokenBegin)),
                        lineNumber);
                }
            } else if (keyword == "usemtl") {
                SkipSpace(ptr, lineEnd);
                while (lineEnd > ptr && IsSpace(lineEnd[-1])) --lineEnd;
                const std::string name(ptr, lineEnd);
                auto found = materialIndexByName.find(name);
                if (found != materialIndexByName.end()) {
                    currentMaterialIndex = found->second;
                } else {
                    Material material;
                    material.name = name;
                    materials.push_back(material);
                    currentMaterialIndex = static_cast<int>(materials.size()) - 1;
                    materialIndexByName.emplace(name, currentMaterialIndex);
                    addWarning(lineNumber, "找不到材质定义，已使用默认白色");
                }
            } else if (keyword == "f") {
                std::vector<FaceVertex>& face = faceScratch;
                face.clear();
                bool validFace = true;
                while (ptr < lineEnd) {
                    SkipSpace(ptr, lineEnd);
                    if (ptr >= lineEnd || *ptr == '#') break;
                    const char* tokenBegin = ptr;
                    while (ptr < lineEnd && !IsSpace(*ptr) && *ptr != '#') ++ptr;
                    FaceVertex faceVertex;
                    if (!ParseFaceVertex(
                            std::string_view(tokenBegin, static_cast<size_t>(ptr - tokenBegin)),
                            positions.size(), texCoords.size(), normals.size(), faceVertex)) {
                        validFace = false;
                        break;
                    }
                    face.push_back(faceVertex);
                }

                if (!validFace || face.size() < 3) {
                    addWarning(lineNumber, "面索引无效或顶点少于 3 个，已跳过");
                    continue;
                }

                const bool faceHasCompleteNormals = std::all_of(
                    face.begin(), face.end(), [&](const FaceVertex& vertex) {
                        return vertex.normal >= 0 &&
                            LengthSquared(normals[vertex.normal]) >
                                std::numeric_limits<float>::epsilon();
                    });
                const Vec3 generatedFaceNormal = faceHasCompleteNormals
                    ? Vec3{}
                    : CalculateFaceNormal(face, positions);
                if (!faceHasCompleteNormals) {
                    if (generatedNormalSums.size() < positions.size()) {
                        generatedNormalSums.resize(positions.size());
                    }
                    for (const FaceVertex& vertex : face) {
                        Vec3& sum = generatedNormalSums[vertex.position];
                        sum.x += generatedFaceNormal.x;
                        sum.y += generatedFaceNormal.y;
                        sum.z += generatedFaceNormal.z;
                    }
                }

                auto createVertex = [&](const FaceVertex& source,
                                        bool useObjNormal) -> uint32_t {
                    if (vertices.size() >= std::numeric_limits<uint32_t>::max()) {
                        throw std::runtime_error("OBJ 顶点数量超过 32 位索引上限");
                    }

                    AssetVertex vertex{};
                    const Vec3& position = positions[source.position];
                    vertex.position[0] = position.x;
                    vertex.position[1] = position.y;
                    vertex.position[2] = position.z;
                    vertex.color[0] = 1.0f;
                    vertex.color[1] = 1.0f;
                    vertex.color[2] = 1.0f;
                    if (source.texCoord >= 0) {
                        vertex.texCoord[0] = texCoords[source.texCoord].u;
                        vertex.texCoord[1] = texCoords[source.texCoord].v;
                    }
                    if (useObjNormal) {
                        const Vec3& normal = normals[source.normal];
                        vertex.normal[0] = normal.x;
                        vertex.normal[1] = normal.y;
                        vertex.normal[2] = normal.z;
                    }

                    const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                    generatedVertexPositions.push_back(static_cast<uint32_t>(source.position));
                    return newIndex;
                };

                auto emitCorner = [&](size_t corner) {
                    const FaceVertex& source = face[corner];
                    if (!faceHasCompleteNormals && source.texCoord < 0) {
                        if (positionOnlyLookup.size() < positions.size()) {
                            positionOnlyLookup.resize(
                                positions.size(), std::numeric_limits<uint32_t>::max());
                        }
                        uint32_t& mappedIndex = positionOnlyLookup[source.position];
                        if (mappedIndex == std::numeric_limits<uint32_t>::max()) {
                            mappedIndex = createVertex(source, false);
                        }
                        indices.push_back(mappedIndex);
                        return;
                    }

                    const VertexKey key = {
                        source.position,
                        source.texCoord,
                        faceHasCompleteNormals ? source.normal : -1
                    };

                    auto found = vertexLookup.find(key);
                    if (found == vertexLookup.end()) {
                        const uint32_t newIndex =
                            createVertex(source, faceHasCompleteNormals);
                        found = vertexLookup.emplace(key, newIndex).first;
                    }
                    indices.push_back(found->second);
                };

                ensureSubMesh(currentMaterialIndex);

                if (face.size() == 3) {
                    emitCorner(0);
                    emitCorner(1);
                    emitCorner(2);
                } else {
                    const auto triangles = TriangulateFace(face, positions);
                    if (triangles.empty()) {
                        addWarning(lineNumber, "面无法三角化，已跳过");
                        continue;
                    }
                    for (const auto& triangle : triangles) {
                        for (size_t corner : triangle) {
                            emitCorner(corner);
                        }
                    }
                }
            }
        }

        if (positions.empty()) {
            throw std::runtime_error("OBJ 文件中没有有效顶点");
        }
        if (vertices.empty() || indices.empty()) {
            throw std::runtime_error("OBJ 文件中没有可渲染的有效面");
        }

        closeOpenSubMesh();

        for (size_t i = 0; i < vertices.size(); ++i) {
            AssetVertex& vertex = vertices[i];
            if (LengthSquared(Vec3{ vertex.normal[0], vertex.normal[1], vertex.normal[2] }) >
                std::numeric_limits<float>::epsilon()) {
                continue;
            }
            if (i >= generatedVertexPositions.size()) {
                continue;
            }
            const uint32_t positionIndex = generatedVertexPositions[i];
            const Vec3 normal = positionIndex < generatedNormalSums.size()
                ? NormalizeOr(generatedNormalSums[positionIndex], {0.0f, 0.0f, 1.0f})
                : Vec3{0.0f, 0.0f, 1.0f};
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
        }

        const Vec3 center = {(bboxMin.x + bboxMax.x) * 0.5f,
                             (bboxMin.y + bboxMax.y) * 0.5f,
                             (bboxMin.z + bboxMax.z) * 0.5f};
        const Vec3 size = {bboxMax.x - bboxMin.x,
                           bboxMax.y - bboxMin.y,
                           bboxMax.z - bboxMin.z};
        const float maxSize = std::max({size.x, size.y, size.z});
        const float scale = maxSize > std::numeric_limits<float>::epsilon()
            ? 2.0f / maxSize : 1.0f;

        // 1.4：颜色改由材质 baseColor（MTL Kd）与纹理决定，顶点色统一为白。
        // （此前烘焙的高度渐变已移除；顶点色保留白色以兼容后续顶点色路径。）
        (void)center;
        (void)scale;
        for (AssetVertex& vertex : vertices) {
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;
        }

        // 组装资产：包围盒、SubMesh 局部包围盒、诊断信息。
        MeshData mesh;
        mesh.name = std::filesystem::u8path(m_filePath).filename().u8string();
        mesh.vertices = std::move(vertices);
        mesh.indices = std::move(indices);
        mesh.subMeshes = std::move(subMeshes);
        mesh.bounds.min = bboxMin;
        mesh.bounds.max = bboxMax;

        for (SubMesh& subMesh : mesh.subMeshes) {
            Vec3 subMin = { std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max() };
            Vec3 subMax = { std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest() };
            const size_t end = static_cast<size_t>(subMesh.indexOffset) + subMesh.indexCount;
            for (size_t i = subMesh.indexOffset; i < end && i < mesh.indices.size(); ++i) {
                const AssetVertex& vertex = mesh.vertices[mesh.indices[i]];
                subMin.x = std::min(subMin.x, vertex.position[0]);
                subMin.y = std::min(subMin.y, vertex.position[1]);
                subMin.z = std::min(subMin.z, vertex.position[2]);
                subMax.x = std::max(subMax.x, vertex.position[0]);
                subMax.y = std::max(subMax.y, vertex.position[1]);
                subMax.z = std::max(subMax.z, vertex.position[2]);
            }
            subMesh.localBounds.min = subMin;
            subMesh.localBounds.max = subMax;
        }

        auto asset = std::make_shared<SceneAsset>();
        asset->sourcePath = m_filePath;
        asset->meshes.push_back(std::move(mesh));
        asset->materials = std::move(materials);
        asset->textures = std::move(textures);
        asset->images = std::move(images);
        asset->messages = std::move(messages);

        auto callback = m_callback;
        QMetaObject::invokeMethod(QApplication::instance(),
            [callback, asset]() {
                if (callback) {
                    callback(std::move(*asset));
                }
            }, Qt::QueuedConnection);
    } catch (const std::exception& exception) {
        report(std::string("[错误] ") + std::filesystem::u8path(m_filePath).filename().u8string()
                   + ": " + exception.what(), true);
    } catch (...) {
        report("[错误] 解析 OBJ 文件时发生未知错误", true);
    }
}
