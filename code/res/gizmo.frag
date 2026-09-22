#version 450
// Gizmo 叠加层片元着色器（任务 2.4）：直接输出插值颜色（无光照）。
layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
