#version 450
// 深度解析（resolve）片元着色器（1.7，仅 Vulkan）。
// 从多重采样深度纹理取平均深度，写入单采样深度附件的 gl_FragDepth。
// 该单采样深度随后供世界坐标回读（vkCmdCopyImageToBuffer）使用。
// 注意：本文件为 UTF-8 无 BOM（glslc 拒绝 BOM）。

layout(binding = 0) uniform sampler2DMS msDepth;

layout(location = 0) in vec2 vUV;

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int samples = textureSamples(msDepth);
    float depth = 0.0;
    for (int i = 0; i < samples; ++i) {
        depth += texelFetch(msDepth, coord, i).r;
    }
    depth /= float(max(samples, 1));
    gl_FragDepth = depth;
}
