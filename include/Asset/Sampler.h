#ifndef __ASSET_SAMPLER_H__
#define __ASSET_SAMPLER_H__

#include <string>

// 采样器描述。字段沿用 glTF 枚举值（1.2 只记录，不创建 GPU 采样器）。
struct Sampler {
    std::string name;
    int wrapS = 10497;       // GL_REPEAT
    int wrapT = 10497;       // GL_REPEAT
    int minFilter = 0;       // 0 表示未指定
    int magFilter = 0;       // 0 表示未指定
};

#endif //__ASSET_SAMPLER_H__
