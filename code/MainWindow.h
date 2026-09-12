#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include <QMainWindow>
#include "VertexType/VertexTypes.h"
#include <vector>
#include <cstdint>
#include <string>
#include <QStringList>

class QWindowVulkan;
class QWindowOpenGL;
class Render;
class Layer;
class Object;
class QTimer;
class QWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QLabel;
class QString;
class QPushButton;
class QComboBox;
class QHBoxLayout;
class QLineEdit;
class QDateEdit;
class QTimeEdit;
class QDial;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onOpenFile();
    void on2DController();
    void on3DController();
    void onLightAnalysis();
    void onRecentFileTriggered();
    void onBackendEngineChanged(int index);

private:
    void SetupToolBar();
    void LoadFile(const QString& filePath);
    void AddRecentFile(const QString& filePath);
    void LoadRecentFiles();
    void SaveRecentFiles();
    void RebuildRecentMenu();
    void SetupStatusBar();
    void SetupVulkan();
    void StartRenderLoop();
    void SwitchTo2D();
    void SwitchTo3D();
    void SwitchToVulkan();
    void SwitchToOpenGL();
    bool IsOpenGLBackend() const;
    void EnsureOpenGLWindow();
    void EnsureOpenGLInitialized();
    void SwitchOpenGLTo2D();
    void SwitchOpenGLTo3D();
    void ApplyLightAnalysisToRenderer();
    void UpdateShadowSceneBounds();
    void SyncShadowTextureSizeCombo(uint32_t size);
    void ShowShadowMapStatus(const std::string& message);
    void AddLoadedModel(const QString& filePath, std::vector<Vertex3D>&& vertices, std::vector<uint32_t>&& indices);
    void SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible);
    void RemoveLoadedModel(QTreeWidgetItem* treeItem);
    void ClearLoadedModels();
    void FocusSceneOrModel(QTreeWidgetItem* treeItem);
    void RebuildSceneMeshes();
    void ResetLoadedMeshPointers();

    struct LoadedModel {
        std::vector<Vertex3D> sourceVertices;
        std::vector<uint32_t> indices;
        Object* mesh = nullptr;            // 所有权属于场景
        QTreeWidgetItem* treeItem = nullptr;    // 所有权属于主面板
        bool visible = true;
    };

    Render* m_renderer;
    Render* m_openglRenderer = nullptr;
    QWindowVulkan* m_vulkanWindow;
    QWindowOpenGL* m_openglWindow = nullptr;
    QWidget* m_container;
    QWidget* m_openglContainer = nullptr;
    QHBoxLayout* m_viewportLayout = nullptr;
    Layer* m_scene;
    QTimer* m_renderTimer;
    QTreeWidget* m_projectPanel;
    QTreeWidgetItem* m_modelsTreeItem;

    std::vector<LoadedModel> m_loadedModels;
    uint64_t m_loadGeneration = 0;
    float m_sceneViewDistance = 3.0f;
    float m_sceneSourceCenter[3] = {};
    float m_sceneNormalizationScale = 1.0f;

    QCheckBox* m_borderCheck = nullptr;
    QCheckBox* m_grayCheck = nullptr;
    QCheckBox* m_dyeCheck = nullptr;
    QCheckBox* m_orthographicCheck = nullptr;
    QPushButton* m_lightAnalysisButton = nullptr;
    QComboBox* m_backendEngineCombo = nullptr;
    QWidget* m_lightAnalysisPanel = nullptr;
    QComboBox* m_pComboTexSize = nullptr;
    QLineEdit* m_pLatitudeEdit = nullptr;
    QDateEdit* m_pDateEdit = nullptr;
    QTimeEdit* m_pTimeEdit = nullptr;
    QDial* m_pTimeDial = nullptr;

    QLabel* m_coordX = nullptr;
    QLabel* m_coordY = nullptr;
    QLabel* m_coordZ = nullptr;

    QMenu* m_recentMenu = nullptr;
    QStringList m_recentFiles;
};

#endif // __MAIN_WINDOW_H__
