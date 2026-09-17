#pragma once

#include <QRunnable>
#include <functional>
#include <string>
#include <vector>
#include "Asset/SceneAsset.h"

class ObjParseRunnable : public QRunnable {
public:
    using Callback = std::function<void(SceneAsset&& asset)>;
    using DiagnosticCallback = std::function<void(const std::string& message, bool isError)>;

    explicit ObjParseRunnable(std::string filePath, Callback callback, DiagnosticCallback diagnosticCallback = {});

    ~ObjParseRunnable() override;
    // 执行后台任务。
    void run() override;

private:
    std::string m_filePath;
    Callback m_callback;
    DiagnosticCallback m_diagnosticCallback;
};
