#version 450
// HDR 后处理片元着色器（1.6）：采样线性 HDR 中间目标 -> 曝光 -> ACES 色调映射 -> 输出。
// 颜色空间约定：
//   * 输入 hdrColor 为线性 HDR（场景在 HDR 目标中按线性空间渲染，未做任何 gamma）。
//   * ACES 输出为显示参考的线性值 [0,1]。
//   * Vulkan：交换链为 B8G8R8A8_SRGB，硬件在写入时做 sRGB 编码，故这里输出线性值。
//   * OpenGL：默认帧缓冲不是 sRGB 帧缓冲，硬件不会编码，故这里手动做线性 -> sRGB。
//     这保证最终输出只做一次 gamma 变换，不会重复转换。
// 注意：本文件为 UTF-8 无 BOM（glslc 拒绝 BOM）。

// 后处理参数（Vulkan push constant / OpenGL 普通 uniform，字段语义一致）。
//   x: 曝光乘数（2^EV，已在线性空间应用）；y: 调试标志（>0.5 时跳过曝光与色调映射，直接输出）；zw 预留。
#ifdef VULKAN
layout(push_constant) uniform PostPush {
    vec4 postParams;
} post;
#define POST_EXPOSURE post.postParams.x
#define POST_DEBUG post.postParams.y
#else
uniform vec4 uPostParams;
#define POST_EXPOSURE uPostParams.x
#define POST_DEBUG uPostParams.y
#endif

layout(binding = 0) uniform sampler2D hdrTexture;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// ACES 电影级色调映射（Narkowicz 2015 拟合）。输入/输出均为线性。
vec3 AcesToneMap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

#ifndef VULKAN
// 线性 -> sRGB 编码（OpenGL 手动编码；Vulkan 由 sRGB 交换链硬件编码）。
vec3 LinearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
    vec3 lower = c * 12.92;
    vec3 higher = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, vec3(cutoff));
}
#endif

void main() {
    vec3 hdrColor = texture(hdrTexture, vUV).rgb;

    vec3 mapped;
    if (POST_DEBUG > 0.5) {
        // 调试视图（1.7）：直接输出原始 [0,1] 值，绕过曝光与色调映射（数值稳定、便于对比）。
        mapped = clamp(hdrColor, 0.0, 1.0);
    } else {
        // 曝光：在线性空间乘 2^EV。高于 1 的亮部在色调映射中平滑滚降，而非直接截断为白。
        hdrColor *= POST_EXPOSURE;
        mapped = AcesToneMap(hdrColor);
    }

#ifdef VULKAN
    // sRGB 交换链硬件负责编码。
    outColor = vec4(mapped, 1.0);
#else
    outColor = vec4(LinearToSrgb(mapped), 1.0);
#endif
}
