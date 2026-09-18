#ifndef __ASSET_IMAGE_H__
#define __ASSET_IMAGE_H__

#include <cstdint>
#include <string>
#include <vector>

// 图像来源描述。1.2 记录声明与来源；1.3 起额外携带已解析的编码字节，
// 供上层解码（PNG/JPEG 等）后上传为 GPU 纹理。
// 本结构保持后端无关：不引入 Qt / Vulkan / OpenGL 头。
struct Image {
    std::string name;
    std::string uri;        // 外部路径或 data URI；空表示来自 bufferView
    int bufferView = -1;    // 内嵌图像所在 bufferView；-1 表示无
    std::string mimeType;   // 例如 image/png
    // 外部图像解析后的绝对路径；内嵌（data URI / bufferView）时为空。
    std::string resolvedPath;
    // 已解析的原始编码字节（PNG/JPEG/... 文件内容），由导入器填充。
    std::vector<uint8_t> encodedData;
};

#endif //__ASSET_IMAGE_H__
