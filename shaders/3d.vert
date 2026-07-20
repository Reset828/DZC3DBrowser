#version 450
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 displayOptions;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragViewPosition;
layout(location = 2) flat out float grayEnabled;
layout(location = 3) out vec3 fragViewNormal;

void main() {
    vec4 viewPosition = ubo.view * ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * viewPosition;
    gl_PointSize = 8.0;

    // 榛樿鏄剧ず绾粦鑹层€?    fragColor = vec3(0.0);
    fragViewPosition = viewPosition.xyz;
    grayEnabled = ubo.displayOptions.x;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
    // 闂嗚泛鎮滈柌蹇氥€冪粈?OBJ 閺堫亝褰佹笟娑欑《缁惧尅绱濋悧鍥у帗閻偓閼规彃娅掔亸鍡樺瘻娑撳顫楅棃銏ゅ櫢瀵ょ儤纭剁痪瑁も偓?    fragViewNormal = normalMatrix * inNormal;
}
