#ifndef __GL_TEXTURE_H__
#define __GL_TEXTURE_H__

#include <cstdint>

#include "Texture/TextureTypes.h"

class QOpenGLFunctions_4_2_Core;

// 一张 OpenGL 2D 纹理：纹理对象 + 采样器对象。
// 与 Vulkan 侧对等（上传、sRGB 内部格式、mipmap 生成），只是不在着色器中采样。
class GLTexture {
public:
    GLTexture() = default;
    ~GLTexture();

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    // 按描述创建纹理；成功返回 true。functions 必须非空且当前上下文已激活。
    bool Create(QOpenGLFunctions_4_2_Core* functions, const TextureDesc& desc);
    // 释放 GPU 资源；可安全重复调用。
    void Destroy();

    // 查询是否已成功创建。
    bool IsValid() const { return m_texture != 0; }

    unsigned int GetTexture() const { return m_texture; }
    unsigned int GetSampler() const { return m_sampler; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint32_t GetMipLevels() const { return m_mipLevels; }
    // 是否使用 sRGB 内部格式。
    bool IsSrgb() const { return m_srgb; }

private:
    // 选择内部格式（颜色纹理 sRGB8，数据纹理 RGBA8）。
    static unsigned int ChooseInternalFormat(TextureSemantic semantic, bool& srgbOut);
    // 创建 GL 采样器对象。
    bool CreateSampler(const SamplerDesc& sampler, bool useMipmaps);

    QOpenGLFunctions_4_2_Core* m_functions = nullptr;
    unsigned int m_texture = 0;
    unsigned int m_sampler = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    bool m_srgb = false;
};

#endif //__GL_TEXTURE_H__
