#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include <QMainWindow>
#include "VertexType/VertexTypes.h"
#include <vector>
#include <cstdint>

class QWindowVulkan;
class VulkanRender;
class VulkanLayer;
class VulkanMesh;
class QTimer;
class QWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QLabel;
class QString;

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

private:
    void SetupToolBar();
    void SetupStatusBar();
    void SetupVulkan();
    void StartRenderLoop();
    void SwitchTo2D();
    void SwitchTo3D();
    void AddLoadedModel(const QString& filePath,
                        std::vector<Vertex3D>&& vertices,
                        std::vector<uint32_t>&& indices);
    void SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible);
    void RemoveLoadedModel(QTreeWidgetItem* treeItem);
    void ClearLoadedModels();
    void FocusSceneOrModel(QTreeWidgetItem* treeItem);
    void RebuildSceneMeshes();
    void ResetLoadedMeshPointers();

    struct LoadedModel {
        std::vector<Vertex3D> sourceVertices;
        std::vector<uint32_t> indices;
        VulkanMesh* mesh = nullptr;             // 所有权属于场景
        QTreeWidgetItem* treeItem = nullptr;    // 所有权属于主面板
        bool visible = true;
    };

    VulkanRender* m_renderer;
    QWindowVulkan* m_vulkanWindow;
    QWidget* m_container;
    VulkanLayer* m_scene;
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

    QLabel* m_coordX = nullptr;
    QLabel* m_coordY = nullptr;
    QLabel* m_coordZ = nullptr;
};

#endif // __MAIN_WINDOW_H__
