#pragma once

#include <QRunnable>
#include <functional>
#include <string>
#include "Asset/SceneAsset.h"

class GltfParseRunnable : public QRunnable {
public:
    using Callback = std::function<void(SceneAsset&& asset)>;
    using DiagnosticCallback = std::function<void(const std::string& message, bool isError)>;

    explicit GltfParseRunnable(std::string filePath, Callback callback, DiagnosticCallback diagnosticCallback = {});

    ~GltfParseRunnable() override;
    // 执行后台任务。
    void run() override;

private:
    std::string m_filePath;
    Callback m_callback;
    DiagnosticCallback m_diagnosticCallback;
};
