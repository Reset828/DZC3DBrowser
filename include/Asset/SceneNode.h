#ifndef __ASSET_NODE_H__
#define __ASSET_NODE_H__

#include <string>
#include <vector>
#include "Math/EngineTypes.h"

// glTF 节点层级元数据。变换已在解析时烘焙进顶点，这里仅保留结构与名称。
struct SceneNode {
    std::string name;
    int parent = -1;              // 父节点索引；-1 表示根
    std::vector<int> children;    // 子节点索引
    Mat4 localTransform;          // 局部变换（原始，未烘焙）
    int mesh = -1;                // 指向 SceneAsset::meshes；-1 表示无
};

#endif //__ASSET_NODE_H__
