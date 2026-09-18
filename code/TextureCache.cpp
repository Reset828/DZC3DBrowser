#include "TextureCache.h"

#include <QFile>
#include <QImage>

#include <cstddef>
#include <cstring>
#include <filesystem>

namespace {

// 把任意 QImage 规范化为 RGBA8（不预乘、紧密排列）。
bool NormalizeToRgba8(const QImage& source, DecodedImage& out) {
    if (source.isNull()) return false;
    QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return false;

    out.width = static_cast<uint32_t>(image.width());
    out.height = static_cast<uint32_t>(image.height());
    const size_t rowBytes = static_cast<size_t>(image.width()) * 4;
    out.pixels.resize(rowBytes * static_cast<size_t>(image.height()));
    for (int y = 0; y < image.height(); ++y) {
        const uchar* scan = image.constScanLine(y);
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * rowBytes, scan, rowBytes);
    }
    return true;
}

} // namespace

// 由编码字节解码为 RGBA8。
bool TextureCache::DecodeToRgba8(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size == 0) return false;
    QImage image = QImage::fromData(data, static_cast<int>(size));
    if (image.isNull()) return false;
    return NormalizeToRgba8(image, out);
}

// 计算内容 FNV-1a 64 位哈希。
uint64_t TextureCache::HashBytes(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

// 由外部路径获取/解码。
const DecodedImage* TextureCache::GetOrDecodeFile(const std::string& path, bool* cacheHit) {
    if (cacheHit) *cacheHit = false;
    if (path.empty()) return nullptr;

    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(std::filesystem::u8path(path), ec);
    const std::string key = "path:" + (ec ? path : canonical.u8string());

    const auto found = m_images.find(key);
    if (found != m_images.end()) {
        if (cacheHit) *cacheHit = true;
        return &found->second;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray bytes = file.readAll();
    file.close();

    DecodedImage image;
    if (!DecodeToRgba8(reinterpret_cast<const uint8_t*>(bytes.constData()),
                       static_cast<size_t>(bytes.size()), image)) {
        return nullptr;
    }
    const auto inserted = m_images.emplace(key, std::move(image));
    return &inserted.first->second;
}

// 由内嵌编码字节获取/解码。
const DecodedImage* TextureCache::GetOrDecodeBytes(const uint8_t* data, size_t size,
                                                   bool* cacheHit) {
    if (cacheHit) *cacheHit = false;
    if (!data || size == 0) return nullptr;

    const uint64_t hash = HashBytes(data, size);
    const std::string key = "hash:" + std::to_string(hash);

    const auto found = m_images.find(key);
    if (found != m_images.end()) {
        if (cacheHit) *cacheHit = true;
        return &found->second;
    }

    DecodedImage image;
    if (!DecodeToRgba8(data, size, image)) return nullptr;
    const auto inserted = m_images.emplace(key, std::move(image));
    return &inserted.first->second;
}
