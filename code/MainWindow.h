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
    // 关闭窗口前停渲染并释放后端。
    void closeEvent(QCloseEvent* event) override;
    // 把鼠标/滚轮事件转给当前渲染器。
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // 弹出对话框打开 OBJ。
    void onOpenFile();
    // 切到二维渲染器。
    void on2DController();
    // 切到三维渲染器。
    void on3DController();
    // 打开或关闭光照分析面板。
    void onLightAnalysis();
    // 打开最近文件菜单项对应路径。
    void onRecentFileTriggered();
    // 在 Vulkan / OpenGL 后端间切换。
    void onBackendEngineChanged(int index);

private:
    // 创建菜单与工具栏控件。
    void SetupToolBar();
    // 后台解析 OBJ 并加入场景。
    void LoadFile(const QString& filePath);
    // 把路径写入最近文件列表。
    void AddRecentFile(const QString& filePath);
    // 从设置读取最近文件。
    void LoadRecentFiles();
    // 把最近文件写回设置。
    void SaveRecentFiles();
    // 重建最近文件菜单。
    void RebuildRecentMenu();
    // 创建坐标状态栏。
    void SetupStatusBar();
    // 创建 Vulkan 窗口容器并接渲染循环。
    void SetupVulkan();
    // 启动约 16ms 的帧定时器。
    void StartRenderLoop();
    // 销毁三维渲染器并换成二维。
    void SwitchTo2D();
    // 销毁二维渲染器并换成三维。
    void SwitchTo3D();
    // 隐藏 OpenGL 视口，恢复 Vulkan。
    void SwitchToVulkan();
    // 隐藏 Vulkan 视口，启用 OpenGL。
    void SwitchToOpenGL();
    // 当前是否显示 OpenGL 视口。
    bool IsOpenGLBackend() const;
    // 按需创建 OpenGL 窗口与容器。
    void EnsureOpenGLWindow();
    // 初始化 OpenGL 渲染器并同步显示选项。
    void EnsureOpenGLInitialized();
    // 把 OpenGL 后端换成二维。
    void SwitchOpenGLTo2D();
    // 把 OpenGL 后端换成三维。
    void SwitchOpenGLTo3D();
    // 把光照面板参数写进当前三维渲染器。
    void ApplyLightAnalysisToRenderer();
    // 用已加载模型包围盒更新阴影范围。
    void UpdateShadowSceneBounds();
    // 同步阴影贴图尺寸下拉框。
    void SyncShadowTextureSizeCombo(uint32_t size);
    // 在状态栏显示阴影贴图消息。
    void ShowShadowMapStatus(const std::string& message);
    // 把解析结果加入模型列表并建网格。
    void AddLoadedModel(const QString& filePath, std::vector<Vertex3D>&& vertices, std::vector<uint32_t>&& indices);
    // 切换模型可见性。
    void SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible);
    // 从场景和列表移除模型。
    void RemoveLoadedModel(QTreeWidgetItem* treeItem);
    // 清空全部已加载模型。
    void ClearLoadedModels();
    // 把相机对准场景或指定模型。
    void FocusSceneOrModel(QTreeWidgetItem* treeItem);
    // 按渲染器类型安装 Mesh 工厂。
    void ConfigureMeshFactory(Render* renderer);
    // 按当前后端重建场景网格。
    void RebuildSceneMeshes();
    // 把模型上的网格指针置空。
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
