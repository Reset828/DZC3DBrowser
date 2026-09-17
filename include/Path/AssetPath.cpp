#include "Path/AssetPath.h"

#include <windows.h>

#include <vector>


namespace {

// 判断给定路径是否为已存在的目录。
bool DirectoryExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 用 Win32 规范化路径（折叠 ".."、"/" 等）。
std::string NormalizePath(const std::string& path) {
    char buffer[MAX_PATH * 4];
    const DWORD length = GetFullPathNameA(
        path.c_str(), static_cast<DWORD>(sizeof(buffer)), buffer, nullptr);
    if (length == 0 || length >= sizeof(buffer)) return path;
    return std::string(buffer, length);
}

}

namespace AssetPath {

std::string ExecutableDir() {
    static const std::string cached = []() -> std::string {
        std::vector<char> buffer(MAX_PATH);
        for (;;) {
            const DWORD length = GetModuleFileNameA(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return std::string();
            if (length < buffer.size()) {
                const std::string full(buffer.data(), length);
                const std::string::size_type slash = full.find_last_of("\\/");
                if (slash == std::string::npos) return full;
                return full.substr(0, slash);
            }
            buffer.resize(buffer.size() * 2);
        }
    }();
    return cached;
}

std::string ShaderDir() {
    static const std::string cached = []() -> std::string {
        const std::string exeDir = ExecutableDir();
        if (exeDir.empty()) return std::string("shaders");
        // exe 位于 windows/Debug 或 windows/Release，着色器位于 windows/shaders。
        // 依次尝试：与 exe 同级、上一层、上两层，命中即用。
        const char* const candidates[] = {
            "\\shaders",
            "\\..\\shaders",
            "\\..\\..\\shaders"
        };
        for (const char* candidate : candidates) {
            const std::string dir = exeDir + candidate;
            if (DirectoryExists(dir)) return NormalizePath(dir);
        }
        return NormalizePath(exeDir + "\\..\\shaders");
    }();
    return cached;
}

std::string Resolve(const std::string& relativeToExeDir) {
    const std::string exeDir = ExecutableDir();
    if (exeDir.empty()) return relativeToExeDir;
    if (relativeToExeDir.empty()) return exeDir;
    const char first = relativeToExeDir.front();
    if (first == '\\' || first == '/') return NormalizePath(exeDir + relativeToExeDir);
    return NormalizePath(exeDir + "\\" + relativeToExeDir);
}

std::string ShaderFile(const std::string& fileName) {
    return ShaderDir() + "\\" + fileName;
}

}
