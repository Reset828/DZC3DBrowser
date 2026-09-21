#ifndef __ASSET_NODE_H__
#define __ASSET_NODE_H__

#include <string>
#include <vector>
#include "Math/EngineTypes.h"

// glTF 节点层级元数据。任务 2.1 起：节点自身的 TRS 作为运行时局部变换保留
// （顶点保留在节点局部空间，不再烘焙世界变换），运行时按层级相乘得到世界矩阵。
struct SceneNode {
    std::string name;
    int parent = -1;              // 父节点索引；-1 表示根
    std::vector<int> children;    // 子节点索引
    Mat4 localTransform;          // 节点自身的局部变换（未烘焙）
    int mesh = -1;                // 指向 SceneAsset::meshes；-1 表示无
};

#endif //__ASSET_NODE_H__
