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
#include <string_view>
#include <unordered_map>

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

bool IsSpace(char c) {
    return c == ' ' || c == '\t';
}

void SkipSpace(const char*& ptr, const char* end) {
    while (ptr < end && IsSpace(*ptr)) {
        ++ptr;
    }
}

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

bool ParseInteger(std::string_view text, int& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

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

Vec3 Subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float LengthSquared(const Vec3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

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

} // namespace

ObjParseRunnable::ObjParseRunnable(std::string filePath, Callback callback,
                                   DiagnosticCallback diagnosticCallback)
    : m_filePath(std::move(filePath))
    , m_callback(std::move(callback))
    , m_diagnosticCallback(std::move(diagnosticCallback)) {
    setAutoDelete(true);
}

ObjParseRunnable::~ObjParseRunnable() = default;

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
        std::vector<Vertex3D> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexLookup;
        std::vector<uint32_t> positionOnlyLookup;

        positions.reserve(static_cast<size_t>(fileSize) / 48);
        vertices.reserve(static_cast<size_t>(fileSize) / 48);
        indices.reserve(static_cast<size_t>(fileSize) / 32);

        Vec3 bboxMin = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
        Vec3 bboxMax = {std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};

        size_t warningCount = 0;
        std::string firstWarning;
        auto addWarning = [&](size_t lineNumber, const char* reason) {
            ++warningCount;
            if (firstWarning.empty()) {
                firstWarning = "第 " + std::to_string(lineNumber) + " 行: " + reason;
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

                auto createVertex = [&](const FaceVertex& source,
                                        bool useObjNormal) -> uint32_t {
                    if (vertices.size() >= std::numeric_limits<uint32_t>::max()) {
                        throw std::runtime_error("OBJ 顶点数量超过 32 位索引上限");
                    }

                    Vertex3D vertex{};
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

        const Vec3 center = {(bboxMin.x + bboxMax.x) * 0.5f,
                             (bboxMin.y + bboxMax.y) * 0.5f,
                             (bboxMin.z + bboxMax.z) * 0.5f};
        const Vec3 size = {bboxMax.x - bboxMin.x,
                           bboxMax.y - bboxMin.y,
                           bboxMax.z - bboxMin.z};
        const float maxSize = std::max({size.x, size.y, size.z});
        const float scale = maxSize > std::numeric_limits<float>::epsilon()
            ? 2.0f / maxSize : 1.0f;

        for (Vertex3D& vertex : vertices) {
            const float normalizedZ = (vertex.position[2] - center.z) * scale;
            const float t = std::clamp(normalizedZ * 0.5f + 0.5f, 0.0f, 1.0f);
            if (t < 0.5f) {
                const float u = t * 2.0f;
                vertex.color[0] = u * 0.6f;
                vertex.color[1] = 0.5f + u * -0.2f;
                vertex.color[2] = 0.05f + u * 0.05f;
            } else {
                const float u = (t - 0.5f) * 2.0f;
                vertex.color[0] = 0.6f + u * 0.3f;
                vertex.color[1] = 0.3f + u * 0.6f;
                vertex.color[2] = 0.1f + u * 0.8f;
            }
        }

        if (warningCount > 0) {
            report("OBJ 已加载，但跳过了 " + std::to_string(warningCount) +
                   " 个无效数据行；首个问题：" + firstWarning, false);
        }

        auto sharedVertices =
            std::make_shared<std::vector<Vertex3D>>(std::move(vertices));
        auto sharedIndices =
            std::make_shared<std::vector<uint32_t>>(std::move(indices));
        auto callback = m_callback;
        QMetaObject::invokeMethod(QApplication::instance(),
            [callback, sharedVertices, sharedIndices, center, scale]() {
                if (callback) {
                    callback(std::move(*sharedVertices), std::move(*sharedIndices),
                             center, scale);
                }
            }, Qt::QueuedConnection);
    } catch (const std::exception& exception) {
        report(exception.what(), true);
    } catch (...) {
        report("解析 OBJ 文件时发生未知错误", true);
    }
}
