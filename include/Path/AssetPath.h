#ifndef __ASSETPATH_H__
#define __ASSETPATH_H__

#include <string>


/******************************************************
    AssetPath              运行时资源路径解析
      基于可执行文件所在目录解析运行时资源，
      不依赖进程当前工作目录（CWD）。
********************************************************/
namespace AssetPath {

    // 返回可执行文件所在目录（绝对路径，末尾不带分隔符）。
    std::string ExecutableDir();

    // 返回运行时着色器目录（exe 目录附近的 shaders 目录）。
    std::string ShaderDir();

    // 把相对路径按可执行文件目录解析为绝对路径。
    std::string Resolve(const std::string& relativeToExeDir);

    // 解析着色器目录下的某个文件为绝对路径。
    std::string ShaderFile(const std::string& fileName);

}

#endif //__ASSETPATH_H__
