#include "VertexTypes.h"


// 返回顶点绑定描述。
VkVertexInputBindingDescription Vertex2D::GetBindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex2D);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

// 返回顶点属性描述。
std::vector<VkVertexInputAttributeDescription> Vertex2D::GetAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(3);

    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(Vertex2D, position);

    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex2D, color);

    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex2D, texCoord);

    return attrs;
}


// 返回顶点绑定描述。
VkVertexInputBindingDescription Vertex3D::GetBindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex3D);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

// 返回顶点属性描述。
std::vector<VkVertexInputAttributeDescription> Vertex3D::GetAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(5);

    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex3D, position);

    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex3D, color);

    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex3D, texCoord);

    attrs[3].binding = 0;
    attrs[3].location = 3;
    attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[3].offset = offsetof(Vertex3D, normal);

    // 切线（location 4）：vec4，xyz 切线 + w 手性。
    attrs[4].binding = 0;
    attrs[4].location = 4;
    attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[4].offset = offsetof(Vertex3D, tangent);

    return attrs;
}
