#ifndef __ASSET_IMAGE_H__
#define __ASSET_IMAGE_H__

#include <string>

// 图像来源描述。1.2 只记录声明与来源，不解码像素（解码与上传归 1.3）。
struct Image {
    std::string name;
    std::string uri;        // 外部路径或 data URI；空表示来自 bufferView
    int bufferView = -1;    // 内嵌图像所在 bufferView；-1 表示无
    std::string mimeType;   // 例如 image/png
};

#endif //__ASSET_IMAGE_H__
