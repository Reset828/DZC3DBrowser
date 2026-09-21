#version 450
// 深度解析（resolve）顶点着色器（1.7，仅 Vulkan）。
// Vulkan 1.0 的 vkCmdResolveImage 不支持深度格式，故用全屏着色器把多重采样深度
// 解析为单采样深度，供“鼠标点取世界坐标”回读使用。全屏三角形由 gl_VertexIndex 生成。
// 注意：本文件为 UTF-8 无 BOM（glslc 拒绝 BOM）。

layout(location = 0) out vec2 vUV;

void main() {
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    vUV = position * 0.5 + 0.5;
}
