#ifndef __ASSET_SCENE_H__
#define __ASSET_SCENE_H__

#include <string>
#include <vector>
#include "Asset/Image.h"
#include "Asset/Material.h"
#include "Asset/MeshData.h"
#include "Asset/Sampler.h"
#include "Asset/SceneNode.h"
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
    std::vector<SceneNode> nodes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<ImportMessage> messages;
};

#endif //__ASSET_SCENE_H__
