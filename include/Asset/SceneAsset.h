#ifndef __ASSET_SCENE_H__
#define __ASSET_SCENE_H__

#include <string>
#include <vector>
#include "Asset/Material.h"
#include "Asset/MeshData.h"
#include "Asset/Texture.h"

// 导入过程中的一条诊断信息。
struct ImportMessage {
    std::string message;
    bool isError = false;
};

// 一个文件导入后的完整资产。
struct SceneAsset {
    std::string sourcePath;
    std::vector<MeshData> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<ImportMessage> messages;
};

#endif //__ASSET_SCENE_H__
