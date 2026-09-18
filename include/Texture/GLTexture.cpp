#include "Texture/GLTexture.h"

#include <QOpenGLFunctions_4_2_Core>

namespace {

// 把 glTF / OpenGL 环绕枚举映射为 GL wrap 参数。
unsigned int ToGlWrap(int wrap) {
    switch (wrap) {
    case 33071: return 0x812F;  // GL_CLAMP_TO_EDGE
    case 33648: return 0x8370;  // GL_MIRRORED_REPEAT
    case 10497:                 // GL_REPEAT
    default:    return 0x2901;  // GL_REPEAT
    }
}

// 最小过滤是否使用 mipmap。
bool MinFilterUsesMipmap(int minFilter) {
    // 9984 NEAREST_MIPMAP_NEAREST, 9985 LINEAR_MIPMAP_NEAREST,
    // 9986 NEAREST_MIPMAP_LINEAR, 9987 LINEAR_MIPMAP_LINEAR
    return minFilter == 9984 || minFilter == 9985 ||
           minFilter == 9986 || minFilter == 9987;
}

} // namespace

GLTexture::~GLTexture() {
    Destroy();
}

// 选择内部格式。
unsigned int GLTexture::ChooseInternalFormat(TextureSemantic semantic, bool& srgbOut) {
    srgbOut = IsSrgbSemantic(semantic);
    return srgbOut ? 0x8C43 /*GL_SRGB8_ALPHA8*/ : 0x8058 /*GL_RGBA8*/;
}

// 按描述创建纹理。
bool GLTexture::Create(QOpenGLFunctions_4_2_Core* functions, const TextureDesc& desc) {
    Destroy();

    if (!functions || desc.width == 0 || desc.height == 0 || desc.pixels == nullptr) {
        return false;
    }
    m_functions = functions;
    m_width = desc.width;
    m_height = desc.height;

    const unsigned int internalFormat = ChooseInternalFormat(desc.semantic, m_srgb);
    const uint32_t mipLevels = desc.generateMipmaps
        ? ComputeMipLevelCount(desc.width, desc.height)
        : 1;

    functions->glGenTextures(1, &m_texture);
    if (m_texture == 0) return false;

    functions->glBindTexture(GL_TEXTURE_2D, m_texture);
    // 上传前设置解包对齐，避免非 4 字节行宽错位。
    functions->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    functions->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
        static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
        GL_RGBA, GL_UNSIGNED_BYTE, desc.pixels);
    functions->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (mipLevels > 1) {
        functions->glGenerateMipmap(GL_TEXTURE_2D);
    }
    functions->glBindTexture(GL_TEXTURE_2D, 0);

    if (!CreateSampler(desc.sampler, mipLevels > 1)) {
        Destroy();
        return false;
    }
    m_mipLevels = mipLevels;
    return true;
}

// 创建 GL 采样器对象。
bool GLTexture::CreateSampler(const SamplerDesc& sampler, bool useMipmaps) {
    if (!m_functions) return false;

    m_functions->glGenSamplers(1, &m_sampler);
    if (m_sampler == 0) return false;

    m_functions->glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S,
        static_cast<GLint>(ToGlWrap(sampler.wrapS)));
    m_functions->glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T,
        static_cast<GLint>(ToGlWrap(sampler.wrapT)));
    m_functions->glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER,
        sampler.magFilter == 9728 ? GL_NEAREST : GL_LINEAR);
    if (!useMipmaps) {
        m_functions->glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER,
            sampler.minFilter == 9728 ? GL_NEAREST : GL_LINEAR);
    } else {
        const int minFilter = MinFilterUsesMipmap(sampler.minFilter)
            ? sampler.minFilter : 9987 /*GL_LINEAR_MIPMAP_LINEAR*/;
        m_functions->glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER,
            static_cast<GLint>(minFilter));
    }
    return true;
}

// 释放 GPU 资源。
void GLTexture::Destroy() {
    if (m_functions) {
        if (m_sampler != 0) {
            m_functions->glDeleteSamplers(1, &m_sampler);
            m_sampler = 0;
        }
        if (m_texture != 0) {
            m_functions->glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
    } else {
        m_sampler = 0;
        m_texture = 0;
    }
    m_srgb = false;
}
