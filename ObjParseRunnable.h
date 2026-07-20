#pragma once

#include <QRunnable>
#include <functional>
#include <string>
#include <vector>
#include "render/VulkanRender.h"
#include "VertexTypes.h"

// OBJ 文件解析 QRunnable
// 在 QThreadPool 线程中读取并解析 .obj 文件，
// 完成后在主线程回调产出 vector<VulkanVertex> + vector<uint32_t>（索引）。
class ObjParseRunnable : public QRunnable {
public:
    using Callback = std::function<void(std::vector<Vertex3D>&&,
                                        std::vector<uint32_t>&&,
                                        const Vec3& sourceCenter,
                                        float normalizationScale)>;
    using DiagnosticCallback = std::function<void(const std::string& message,
                                                  bool isError)>;

    explicit ObjParseRunnable(std::string filePath, Callback callback,
                              DiagnosticCallback diagnosticCallback = {});
    ~ObjParseRunnable() override;
    void run() override;

private:
    std::string m_filePath;
    Callback m_callback;
    DiagnosticCallback m_diagnosticCallback;
};

