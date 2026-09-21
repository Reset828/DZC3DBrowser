#version 450
// 全屏三角形后处理顶点着色器（1.6）。
// 不绑定顶点缓冲/索引缓冲：用内置顶点序号生成覆盖整个 NDC 的大三角形，
// 片元着色器据此采样 HDR 中间目标并做曝光 + 色调映射。
// 注意：本文件为 UTF-8 无 BOM（glslc 拒绝 BOM）。

layout(location = 0) out vec2 vUV;

void main() {
    // 3 个顶点覆盖整个屏幕（比一个全屏四边形少一次三角形拼接）。
    // Vulkan/SPIR-V 的内置名为 gl_VertexIndex；桌面 OpenGL 的 GLSL 为 gl_VertexID。
    // glslc 会定义 VULKAN，OpenGL 驱动不会，故用 #ifdef 选择正确名字。
#ifdef VULKAN
    const int vertexIndex = gl_VertexIndex;
#else
    const int vertexIndex = gl_VertexID;
#endif
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[vertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    // NDC -> UV。Vulkan 与 OpenGL 的 NDC Y 方向相反，但两边的帧缓冲/纹理原点
    // 约定也随之相反，故同一映射对两者都正确（场景通道各自写入 HDR 目标时方向一致）。
    vUV = position * 0.5 + 0.5;
}
