#pragma once

#include <QRunnable>
#include <functional>
#include <string>
#include <vector>
#include "render/VulkanRender.h"
#include "VertexTypes.h"

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

