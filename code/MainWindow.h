#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include <QMainWindow>
#include "Asset/SceneAsset.h"
#include "Gizmo/Gizmo.h"
#include "Math/Aabb.h"
#include "Math/Transform.h"
#include "Render/Render.h"
#include "TextureImport.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include <QStringList>
#include <QElapsedTimer>
#include <QColor>

class QWindowVulkan;
class QWindowOpenGL;
class Render;
class Layer;
class Object;
class TextureCache;
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
class QListWidget;
class QSplitter;
class QSlider;
class QDoubleSpinBox;



class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // 渲染节拍（毫秒）。1 = 不封顶；实际帧率仍受 vsync / GPU 限制。
    static constexpr int kRenderTickMs = 1;
    // 阴影通道重画间隔（毫秒）。节拍快于此时跳过重画，复用现有阴影贴图。
    static constexpr int kShadowPassIntervalMs = 100;
    // FPS / 帧时间文字刷新间隔（毫秒）。数值仍按每拍计算。
    static constexpr int kStatsRefreshMs = 250;

    // 运行时网格对象：一个网格 GPU 对象及其来源资产网格索引（任务 2.1）。
    struct RuntimeMesh {
        Object* object = nullptr;   // 所有权属于场景图
        int assetMeshIndex = -1;    // 指向 SceneAsset::meshes
    };

    // 运行时节点：一个可变换的场景节点（Layer），可挂网格对象与子节点（任务 2.1）。
    // nodes[0] 恒为模型根节点。局部变换在“原始模型坐标”空间，按父链相乘得到世界矩阵。
    // 逻辑结构（name/parent/children/meshes/local）在后端切换重建后保留；
    // object 指针在每次重建时重新创建。
    struct RuntimeNode {
        Layer* object = nullptr;              // 场景节点（Layer），所有权属于场景图
        std::string name;
        int parent = -1;                       // 运行时节点索引；-1 表示无父
        std::vector<int> children;             // 子节点运行时索引
        std::vector<RuntimeMesh> meshes;       // 挂在本节点上的网格对象
        Transform local;                       // 可编辑局部变换（模型坐标空间）
        Transform defaultLocal;                // 默认局部变换（“重置变换”恢复到此值）
        bool isClone = false;                  // 是否为“复制”产生的节点
        int sourceAssetNode = -1;              // 对应/来源的 SceneAsset::nodes 索引；-1 表示无
    };

    struct LoadedModel {
        SceneAsset asset;
        std::vector<Object*> meshes;           // 所有网格对象（扁平列表，便于可见性/纹理处理）
        std::vector<RuntimeNode> nodes;        // 运行时节点层级；nodes[0] = 模型根
        std::vector<char> nodeDeleted;         // 与 nodes 平行：节点是否被删除（含子树）
        QTreeWidgetItem* treeItem = nullptr;    // 模型项，所有权属于主面板
        bool visible = true;
        // 每个材质引用的 baseColor 纹理对应一张 GPU 纹理句柄（1.3）。
        std::unordered_map<int, TextureHandle> textureHandles;
        // 是否为当前后端完成过纹理导入（避免重建时重复解码/上传）。
        bool texturesImported = false;
        // 材质可见性（1.4）：键为 SceneAsset::materials 索引；缺省视为可见。
        std::unordered_map<int, bool> materialVisible;
    };

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
    // 打开或关闭材质可见性面板。
    void onMaterialPanel();
    // 打开或关闭 PBR 调试面板。
    void onPbrPanel();
    // 打开或关闭 HDR/曝光面板。
    void onHdrPanel();
    // 打开或关闭调试视图面板。
    void onDebugPanel();
    // 打开或关闭变换编辑面板（任务 2.1）。
    void onTransformPanel();
    // 任务 2.4：切换 Gizmo 模式（平移 / 旋转 / 缩放）。
    void onGizmoTranslate();
    void onGizmoRotate();
    void onGizmoScale();
    // 任务 2.4：撤销 / 重做上一次 Transform 修改。
    void onUndoTransform();
    void onRedoTransform();
    // 帮助菜单：关于（版本信息）。
    void onAbout();
    // 帮助菜单：快捷键（当前程序已有快捷键）。
    void onShortcuts();
    // 打开最近文件菜单项对应路径。
    void onRecentFileTriggered();
    // 在 Vulkan / OpenGL 后端间切换。
    void onBackendEngineChanged(int index);

private:
    // 创建菜单与工具栏控件。
    void SetupToolBar();
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
    // 刷新状态栏后端名称，并清空上次世界坐标。
    void UpdateBackendStatus();
    // 刷新 FPS 与 CPU 帧时间标签。
    void UpdateFrameStats(bool drewFrame, bool paused,
                          qint64 tickStartNs, qint64 tickEndNs);
    // 创建 Vulkan 窗口容器并接渲染循环。
    void SetupVulkan();
    // 启动约 16ms 的帧定时器。
    void StartRenderLoop();
    // 弹出渲染器初始化或着色器加载失败说明。
    void ReportRendererError(Render* renderer, const QString& stage);
    // 设备丢失时停循环并提示重启。
    void HandleDeviceLost(Render* renderer);
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
    // 把 PBR 调试面板（金属度/粗糙度/自发光覆盖）参数写进当前三维渲染器。
    void ApplyPbrToRenderer();
    // 把 HDR/曝光面板（HDR 开关 + 曝光 EV）参数写进当前三维渲染器。
    void ApplyHdrToRenderer();
    // 把调试视图面板（调试视图 + 4x MSAA）参数写进当前三维渲染器。
    void ApplyDebugToRenderer();
    // 用已加载模型包围盒更新阴影范围。
    void UpdateShadowSceneBounds();
    // 同步阴影贴图尺寸下拉框。
    void SyncShadowTextureSizeCombo(uint32_t size);
    // 在状态栏显示阴影贴图消息。
    void ShowShadowMapStatus(const std::string& message);
    // 在消息区追加一条提示（isError 用红色）。
    void ShowMessage(const QString& text, bool isError);
    // 逐条追加显示导入诊断（不清空，消息区为只追加日志）。
    void ShowImportMessages(const std::vector<ImportMessage>& messages);
    // 按扩展名选择解析器并异步加载。
    void LoadSceneAssetFile(const QString& filePath);
    // 把解析结果加入模型列表并建网格。
    void AddLoadedModel(const QString& filePath, SceneAsset&& asset);
    // 切换模型可见性。
    void SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible);
    // 从场景和列表移除模型。
    void RemoveLoadedModel(QTreeWidgetItem* treeItem);
    // 清空全部已加载模型。
    void ClearLoadedModels();
    // 复位场景取景：相机、归一化与坐标显示（无模型或需要回到默认状态时使用）。
    void ResetSceneView();
    // 把相机对准场景或指定模型。
    void FocusSceneOrModel(QTreeWidgetItem* treeItem);
    // 按渲染器类型安装 Mesh 工厂。
    void ConfigureMeshFactory(Render* renderer);
    // 按当前后端重建场景网格（默认重算归一化）。
    void RebuildSceneMeshes();
    // 按当前后端重建场景网格；recomputeNormalization 为 true 时重算场景归一化。
    void RebuildSceneMeshes(bool recomputeNormalization);
    // 把模型上的网格指针置空。
    void ResetLoadedMeshPointers();
    // 按当前后端为所有已加载模型导入纹理（CPU 缓存 + GPU 上传）。
    void ImportModelTextures();
    // 释放指定模型的 GPU 纹理引用（进入渲染器延迟销毁队列）。
    void ReleaseModelTextures(LoadedModel& model);
    // 释放全部模型纹理引用（切换后端/清空场景时）。
    void ReleaseAllModelTextures();
    // 重建材质面板内容（按模型分组 + 每材质复选框）。
    void RebuildMaterialPanel();
    // 应用某模型的材质可见性到其网格。
    void ApplyModelMaterialVisibility(LoadedModel& model);
    // 为所有已加载模型的网格构建逐 SubMesh 绘制信息。
    void RebuildSubMeshDrawInfos();

    // ---------------- 任务 2.1：运行时节点层级 / 变换编辑 ----------------
    // 为一个模型构建运行时节点层级（模型根 + 各资产节点 + 网格挂载）。
    void BuildModelHierarchy(LoadedModel& model);
    // 为一个网格资产创建网格对象（按当前后端）。
    Object* CreateMeshObject(LoadedModel& model, int assetMeshIndex);
    // 计算某运行时节点在“模型空间”（含模型根变换，不含场景归一化）的世界矩阵。
    Mat4 ModelSpaceNodeMatrix(const LoadedModel& model, int runtimeNode) const;
    // 计算场景归一化：更新 m_sceneSourceCenter / m_sceneNormalizationScale。
    void ComputeSceneNormalization();
    // 场景归一化矩阵 M_norm（原始模型坐标 -> 归一化场景空间）。
    Mat4 SceneNormalizationMatrix() const;
    // 累计某模型所有网格在“归一化场景空间”的包围盒（含场景归一化）。
    void AccumulateModelBounds(const LoadedModel& model, Vec3& boundsMin, Vec3& boundsMax,
                               bool& any) const;
    // 计算某模型在“归一化场景空间”的整体世界包围盒（任务 2.2）；无几何返回空盒。
    Aabb ModelWorldBounds(const LoadedModel& model) const;
    // 计算整个场景在“归一化场景空间”的世界包围盒（所有模型并集，任务 2.2）。
    Aabb SceneWorldBounds() const;
    // 在消息栏输出某模型的包围盒（归一化场景空间，任务 2.2，仅导入时调用）。
    void ShowModelBounds(const LoadedModel& model);
    // 重建项目树的节点层级（在每个模型项下填充节点）。
    void RebuildProjectTree();
    // 把某模型节点层级写入项目树。
    void PopulateNodeTreeItems(size_t modelIndex, int runtimeNode, QTreeWidgetItem* parentItem);
    // 项目树选择变化：刷新变换面板。
    void OnProjectSelectionChanged();
    // 把变换面板数值写入当前选中节点。
    void ApplyTransformPanelToSelection();
    // 用选中节点的变换刷新变换面板。
    void SyncTransformPanelFromSelection();
    // 选中节点：重置变换。
    void ResetSelectedNodeTransform();
    // 选中节点：删除（含子树）。
    void DeleteSelectedNode();
    // 选中节点：复制（含子树）。
    void CopySelectedNode();
    // 解析项目树项对应的 (模型索引, 运行时节点索引)；无效返回 false。
    bool ResolveTreeItem(QTreeWidgetItem* item, size_t& modelIndex, int& runtimeNode) const;
    // 找到项目树项对应的模型索引；无效返回 -1。
    int FindModelIndexByTreeItem(QTreeWidgetItem* item) const;
    // 变换变更后：刷新阴影范围（含归一化场景空间包围盒）。
    void RefreshAfterTransformChange();
    // 深拷贝某运行时节点子树；返回新根运行时索引（model.nodes 扩容后重新索引）。
    int CloneRuntimeSubtree(LoadedModel& model, int srcRuntimeNode, int newParent,
                            const Vec3& translationOffset);
    // 标记某运行时节点及其整棵子树为“已删除”。
    void MarkSubtreeDeleted(LoadedModel& model, int runtimeNode);

    // ---------------- 任务 2.4：Transform Gizmo 与编辑闭环 ----------------
    // 把 Gizmo 模式 / 吸附 / 撤销按钮的选中态同步到 UI。
    void SyncGizmoUi();
    // 构建当前选中节点的 Gizmo 放置帧（归一化场景空间，固定屏幕尺寸）；无选中返回 false。
    bool BuildGizmoFrame(GizmoFrame& outFrame) const;
    // 每帧把 Gizmo 线段几何提交给当前活动渲染器（无选中/无 Gizmo 时提交空）。
    void UpdateGizmoGeometry();
    // 视口鼠标按下：优先尝试抓取 Gizmo 轴；返回 true 表示事件已被 Gizmo 消费（不再转相机）。
    bool TryBeginGizmoDrag(float nx, float ny);
    // 视口鼠标拖动：Gizmo 拖拽中则应用变换并返回 true。
    bool TryUpdateGizmoDrag(float nx, float ny);
    // 视口鼠标移动（非拖拽）：更新 Gizmo 悬停轴（用于高亮）。
    void UpdateGizmoHover(float nx, float ny);
    // 视口鼠标松开：结束 Gizmo 拖拽（若有），并提交一条撤销记录。
    void EndGizmoDrag();
    // 当前是否处于 Gizmo 拖拽中。
    bool IsGizmoDragging() const { return m_gizmoDragging; }
    // 选中节点局部变换的“快照”（撤销/重做用）。
    struct TransformSnapshot {
        int model = -1;
        int node = -1;
        Transform local;
    };
    // 记录一条撤销记录（清空重做栈）；合并连续拖拽由 mergeKey 控制。
    void PushUndoSnapshot(const TransformSnapshot& snapshot, bool coalesce);
    // 把快照应用到场景并刷新面板 / 高亮 / 阴影。
    void ApplyTransformSnapshot(const TransformSnapshot& snapshot);
    // 把归一化场景空间向量换算到某运行时节点的父空间（Gizmo 轴 -> 局部平移/旋转用）。
    Vec3 SceneVectorToParentSpace(const LoadedModel& model, int runtimeNode,
                                  const Vec3& v) const;
    // 把归一化场景空间点换算到某运行时节点的父空间（Gizmo 枢轴 -> 局部平移用）。
    Vec3 ScenePointToParentSpace(const LoadedModel& model, int runtimeNode,
                                 const Vec3& p) const;
    // 某运行时节点（含整棵子树）在“归一化场景空间”的包围盒；无几何返回空盒。
    Aabb NodeSubtreeWorldBounds(const LoadedModel& model, int runtimeNode) const;

    // Gizmo 轴长（屏幕像素）与拾取半径（屏幕像素）。
    static constexpr float kGizmoPixelLength = 90.0f;
    static constexpr float kGizmoPickPixels = 8.0f;
    // 撤销栈上限。
    static constexpr size_t kMaxUndoSteps = 128;

    // ---------------- 任务 2.3：选中高亮（由项目树驱动） ----------------
    // 把高亮状态应用到场景对象（选中节点整棵子树高亮，其余清除）。
    void ApplySelectionHighlight();
    // 点击项目树空白处：清除选择并（确有选中时）提示“已取消选择”。
    void ClearSelectionByEmptyClick();
    // 返回某运行时节点的显示名（模型根或无名时用模型文件名）。
    QString NodeDisplayName(int modelIndex, int runtimeNode) const;

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
    QPushButton* m_materialButton = nullptr;
    // PBR 调试面板（1.5）：金属度 / 粗糙度 / 自发光覆盖开关 + 滑块。
    QPushButton* m_pbrButton = nullptr;
    QWidget* m_pbrPanel = nullptr;
    QCheckBox* m_metallicOverrideCheck = nullptr;
    QSlider* m_metallicSlider = nullptr;
    QCheckBox* m_roughnessOverrideCheck = nullptr;
    QSlider* m_roughnessSlider = nullptr;
    QCheckBox* m_emissiveOverrideCheck = nullptr;
    QSlider* m_emissiveSlider = nullptr;
    // HDR/曝光面板（1.6）：HDR 开关 + 曝光 EV 滑块。
    QPushButton* m_hdrButton = nullptr;
    QWidget* m_hdrPanel = nullptr;
    QCheckBox* m_hdrCheck = nullptr;
    QSlider* m_exposureSlider = nullptr;
    QLabel* m_exposureValueLabel = nullptr;
    // 调试视图面板（1.7）：调试视图下拉 + 4x MSAA 开关。
    QPushButton* m_debugButton = nullptr;
    QWidget* m_debugPanel = nullptr;
    QComboBox* m_debugViewCombo = nullptr;
    QCheckBox* m_msaaCheck = nullptr;
    QLabel* m_debugHintLabel = nullptr;
    // 光照分析面板新增（1.7）：阴影偏移 / PCF / 法线偏移 + 半球环境光。
    QSlider* m_pShadowBiasSlider = nullptr;
    QComboBox* m_pPcfCombo = nullptr;
    QSlider* m_pNormalOffsetSlider = nullptr;
    QPushButton* m_pSkyColorButton = nullptr;
    QPushButton* m_pGroundColorButton = nullptr;
    QSlider* m_pAmbientIntensitySlider = nullptr;
    QColor m_ambientSkyColor;
    QColor m_ambientGroundColor;
    QComboBox* m_backendEngineCombo = nullptr;
    QWidget* m_lightAnalysisPanel = nullptr;
    QWidget* m_materialPanel = nullptr;
    QTreeWidget* m_materialTree = nullptr;
    QComboBox* m_pComboTexSize = nullptr;
    QLineEdit* m_pLatitudeEdit = nullptr;
    QDateEdit* m_pDateEdit = nullptr;
    QTimeEdit* m_pTimeEdit = nullptr;
    QDial* m_pTimeDial = nullptr;

    // 变换编辑面板（任务 2.1）：平移 / 旋转（度）/ 缩放 数值输入。
    QPushButton* m_transformButton = nullptr;
    QWidget* m_transformPanel = nullptr;
    QLabel* m_transformSelectionLabel = nullptr;
    QDoubleSpinBox* m_spinTransX = nullptr;
    QDoubleSpinBox* m_spinTransY = nullptr;
    QDoubleSpinBox* m_spinTransZ = nullptr;
    QDoubleSpinBox* m_spinRotX = nullptr;
    QDoubleSpinBox* m_spinRotY = nullptr;
    QDoubleSpinBox* m_spinRotZ = nullptr;
    QDoubleSpinBox* m_spinScaleX = nullptr;
    QDoubleSpinBox* m_spinScaleY = nullptr;
    QDoubleSpinBox* m_spinScaleZ = nullptr;
    // 当前选中节点：模型索引 + 运行时节点索引（-1 = 未选中）。
    int m_selectedModel = -1;
    int m_selectedRuntimeNode = -1;
    bool m_syncingTransformPanel = false;

    // ---------------- 任务 2.4：Transform Gizmo 与编辑闭环 ----------------
    QComboBox* m_gizmoModeCombo = nullptr;         // Gizmo 模式选择器（平移 / 旋转 / 缩放）
    QCheckBox* m_gizmoWorldSpaceCheck = nullptr;   // 勾选 = 世界坐标模式，默认局部
    QCheckBox* m_gizmoSnapCheck = nullptr;         // 吸附开关（默认关）
    QDoubleSpinBox* m_gizmoSnapTranslate = nullptr;
    QDoubleSpinBox* m_gizmoSnapRotate = nullptr;
    QDoubleSpinBox* m_gizmoSnapScale = nullptr;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    bool m_gizmoWorldSpace = false;                // 默认局部坐标
    bool m_gizmoSnapEnabled = false;               // 默认关闭吸附
    // 拖拽状态。
    bool m_gizmoDragging = false;
    GizmoAxis m_gizmoDragAxis = GizmoAxis::None;
    GizmoFrame m_gizmoDragFrame;                   // 拖拽开始时的放置帧（固定屏幕尺寸）
    // 拖拽枢轴（= 子树包围盒中心，归一化场景空间）换算到“节点父空间”的坐标；
    // 旋转/缩放时绕该点进行（保持枢轴不动）。
    Vec3 m_gizmoDragPivotParent = { 0.0f, 0.0f, 0.0f };
    float m_gizmoDragStartParam = 0.0f;            // 平移/缩放的起始轴参数
    float m_gizmoDragStartAngle = 0.0f;            // 旋转的起始方位角
    Vec3 m_gizmoDragStartTranslation = { 0.0f, 0.0f, 0.0f };
    Vec3 m_gizmoDragStartRotation = { 0.0f, 0.0f, 0.0f };
    Vec3 m_gizmoDragStartScale = { 1.0f, 1.0f, 1.0f };
    bool m_gizmoDragRecorded = false;              // 本次拖拽是否已记录撤销
    GizmoAxis m_gizmoHoverAxis = GizmoAxis::None;  // 当前悬停轴（用于高亮）
    // 撤销 / 重做栈（仅覆盖 Transform 修改）。
    std::vector<TransformSnapshot> m_undoStack;
    std::vector<TransformSnapshot> m_redoStack;
    bool m_undoCoalescing = false;                 // 同一连续拖拽是否已入栈

    QLabel* m_backendLabel = nullptr;
    QLabel* m_coordX = nullptr;
    QLabel* m_coordY = nullptr;
    QLabel* m_coordZ = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_frameTimeLabel = nullptr;
    QElapsedTimer m_frameClock;
    QElapsedTimer m_lastShadowPassTimer;
    qint64 m_lastDrawnNs = 0;
    bool m_hasLastDrawnFrame = false;
    qint64 m_lastStatsTextNs = 0;
    bool m_statsPausedShown = false;

    QSplitter* m_messageSplitter = nullptr;
    QListWidget* m_messageList = nullptr;

    // 纹理 CPU 缓存（常驻，跨模型与后端切换存活）。
    std::unique_ptr<TextureCache> m_textureCache;

    QMenu* m_recentMenu = nullptr;
    QStringList m_recentFiles;
};

#endif // __MAIN_WINDOW_H__
