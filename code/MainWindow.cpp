#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "QWindowOpenGL.h"
#include "Render/Render.h"
#include "Render/VKRender.h"
#include "Render/GLRender.h"
#include "Layer/Layer.h"
#include "VKMesh.h"
#include "GLMesh.h"
#include "ObjParseRunnable.h"
#include "GltfParseRunnable.h"
#include "TextureCache.h"
#include "TextureImport.h"
#include "MeshDrawInfo.h"
#include <QWindow>
#include <QString>
#include <QAction>
#include <QMenuBar>
#include <QToolBar>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSizePolicy>
#include <QStatusBar>
#include <QWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <QCloseEvent>
#include <QThreadPool>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QToolButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QListWidget>
#include <QList>
#include <QStyleFactory>
#include <QStyle>
#include <QColor>
#include <QBrush>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDial>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QColorDialog>
#include <QDate>
#include <QTime>
#include <QtGlobal>
#include <QSignalBlocker>
#include <QSettings>
#include <QFile>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <string>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new VKRender3D())
    , m_openglRenderer(nullptr)
    , m_vulkanWindow(nullptr)
    , m_openglWindow(nullptr)
    , m_container(nullptr)
    , m_openglContainer(nullptr)
    , m_viewportLayout(nullptr)
    , m_scene(new Layer())
    , m_renderTimer(nullptr)
    , m_modelsTreeItem(nullptr)
{
    resize(1280, 720);
    m_textureCache = std::make_unique<TextureCache>();
    ConfigureMeshFactory(m_renderer);
    SetupToolBar();
    SetupVulkan();

    connect(m_borderCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
            render3D->SetWireframeEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
            render3D->SetWireframeEnabled(checked);
        }
    });

    connect(m_grayCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
            render3D->SetGrayEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
            render3D->SetGrayEnabled(checked);
        }
    });

    connect(m_dyeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
            render3D->SetDyeEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
            render3D->SetDyeEnabled(checked);
        }
    });

    connect(m_orthographicCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
            render3D->SetOrthographicEnabled(checked);
            if (!checked) {
                render3D->ResetView(m_sceneViewDistance);
            }
        }
        if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
            render3D->SetOrthographicEnabled(checked);
            if (!checked) {
                render3D->ResetView(m_sceneViewDistance);
            }
        }
    });

    connect(m_backendEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBackendEngineChanged);

    // 任务 2.4：Gizmo 模式快捷键（W 平移 / E 旋转 / R 缩放）与撤销重做（Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z）。
    QShortcut* shortcutTranslate = new QShortcut(QKeySequence(Qt::Key_W), this);
    connect(shortcutTranslate, &QShortcut::activated, this, &MainWindow::onGizmoTranslate);
    QShortcut* shortcutRotate = new QShortcut(QKeySequence(Qt::Key_E), this);
    connect(shortcutRotate, &QShortcut::activated, this, &MainWindow::onGizmoRotate);
    QShortcut* shortcutScale = new QShortcut(QKeySequence(Qt::Key_R), this);
    connect(shortcutScale, &QShortcut::activated, this, &MainWindow::onGizmoScale);
    QShortcut* shortcutUndo = new QShortcut(QKeySequence::Undo, this);
    connect(shortcutUndo, &QShortcut::activated, this, &MainWindow::onUndoTransform);
    QShortcut* shortcutRedo = new QShortcut(QKeySequence::Redo, this);
    connect(shortcutRedo, &QShortcut::activated, this, &MainWindow::onRedoTransform);
    QShortcut* shortcutRedoAlt = new QShortcut(
        QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Z), this);
    connect(shortcutRedoAlt, &QShortcut::activated, this, &MainWindow::onRedoTransform);

    SetupStatusBar();
    SyncGizmoUi();

    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background-color: #1e1e1e; }
        QWidget { background-color: #1e1e1e; color: #d4d4d4; }
        QTreeWidget { background-color: #252526; color: #d4d4d4; border: none; outline: none; show-decoration-selected: 0; }
        QTreeWidget::item:hover { background-color: #2a2d2e; }
        QTreeWidget::item:selected { background-color: #094771; }
        QHeaderView::section { background-color: #2d2d2d; color: #d4d4d4; border: none; padding: 4px; }
        QSplitter::handle { background-color: #3c3c3c; }
        QMenuBar { background-color: #2d2d2d; color: #d4d4d4; border: none; padding: 2px; }
        QToolBar { background-color: #252526; border: none; spacing: 8px; padding: 2px 4px; }
        QCheckBox { color: #d4d4d4; spacing: 4px; }
        QStatusBar { background-color: #2d2d2d; color: #888888; font-size: 12px; }
        QStatusBar QLabel { background: transparent; border: none; padding: 0 6px; }
        QStatusBar::item { border: none; background: transparent; }
        QMenuBar::item { background-color: transparent; color: #d4d4d4; padding: 4px 12px; }
        QMenuBar::item:selected { background-color: #3c3c3c; }
        QMenu { background-color: #2d2d2d; color: #d4d4d4; border: 1px solid #3c3c3c; }
        QMenu::item:selected { background-color: #094771; }
    )"));
}

MainWindow::~MainWindow() {
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    if (m_openglRenderer && m_openglRenderer->IsInitialized()) {
        m_openglRenderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
    }
    delete m_scene;
    m_scene = nullptr;

    if (m_renderer) {
        m_renderer->Shutdown();
        delete m_renderer;
        m_renderer = nullptr;
    }
    if (m_openglRenderer) {
        m_openglRenderer->Shutdown();
        delete m_openglRenderer;
        m_openglRenderer = nullptr;
    }
}

// 关闭窗口前停渲染并释放后端。
void MainWindow::closeEvent(QCloseEvent* event) {
    ++m_loadGeneration;
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    if (m_renderer) {
        m_renderer->Quiesce();
        if (m_scene) m_scene->Clear();
        m_renderer->Shutdown();
    }
    if (m_openglRenderer) {
        m_openglRenderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
    }
    QMainWindow::closeEvent(event);
}

// 创建菜单与工具栏控件。
void MainWindow::SetupToolBar() {
    QMenuBar* mb = menuBar();

    QMenu* fileMenu = mb->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(QStringLiteral("打开文件"), this, &MainWindow::onOpenFile);
 
    m_recentMenu = new QMenu(QStringLiteral("最近打开文件"), this);
    m_recentMenu->setToolTipsVisible(true);
    fileMenu->addMenu(m_recentMenu);
    LoadRecentFiles();
    RebuildRecentMenu();

    // 帮助菜单：关于 + 快捷键。
    QMenu* helpMenu = mb->addMenu(QStringLiteral("帮助"));
    helpMenu->addAction(QStringLiteral("关于"), this, &MainWindow::onAbout);
    helpMenu->addAction(QStringLiteral("快捷键"), this, &MainWindow::onShortcuts);

    QToolBar* toolbar = addToolBar(QStringLiteral("工具"));
    toolbar->setMovable(false);

    m_borderCheck = new QCheckBox(QStringLiteral("线框模式"));
    m_borderCheck->setLayoutDirection(Qt::RightToLeft);
    toolbar->addWidget(m_borderCheck);

    m_grayCheck = new QCheckBox(QStringLiteral("显示灰色"));
    m_grayCheck->setLayoutDirection(Qt::RightToLeft);
    toolbar->addWidget(m_grayCheck);

    m_dyeCheck = new QCheckBox(QStringLiteral("染色"));
    m_dyeCheck->setLayoutDirection(Qt::RightToLeft);
    toolbar->addWidget(m_dyeCheck);

    m_orthographicCheck = new QCheckBox(QStringLiteral("正射模式"));
    m_orthographicCheck->setLayoutDirection(Qt::RightToLeft);
    m_orthographicCheck->setChecked(false);
    toolbar->addWidget(m_orthographicCheck);

    m_materialButton = new QPushButton(QStringLiteral("材质"));
    toolbar->addWidget(m_materialButton);
    connect(m_materialButton, &QPushButton::clicked, this, &MainWindow::onMaterialPanel);

    m_lightAnalysisButton = new QPushButton(QStringLiteral("光照分析"));
    toolbar->addWidget(m_lightAnalysisButton);
    connect(m_lightAnalysisButton, &QPushButton::clicked, this, &MainWindow::onLightAnalysis);

    m_pbrButton = new QPushButton(QStringLiteral("PBR 参数"));
    toolbar->addWidget(m_pbrButton);
    connect(m_pbrButton, &QPushButton::clicked, this, &MainWindow::onPbrPanel);

    m_hdrButton = new QPushButton(QStringLiteral("HDR/曝光"));
    toolbar->addWidget(m_hdrButton);
    connect(m_hdrButton, &QPushButton::clicked, this, &MainWindow::onHdrPanel);

    m_debugButton = new QPushButton(QStringLiteral("调试视图"));
    toolbar->addWidget(m_debugButton);
    connect(m_debugButton, &QPushButton::clicked, this, &MainWindow::onDebugPanel);

    m_transformButton = new QPushButton(QStringLiteral("变换"));
    toolbar->addWidget(m_transformButton);
    connect(m_transformButton, &QPushButton::clicked, this, &MainWindow::onTransformPanel);

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    QWidget* engineHost = new QWidget();
    QHBoxLayout* engineLayout = new QHBoxLayout(engineHost);
    engineLayout->setContentsMargins(0, 0, 8, 0);
    engineLayout->setSpacing(6);
    QLabel* engineLabel = new QLabel(QStringLiteral("后端引擎"));
    m_backendEngineCombo = new QComboBox();
    m_backendEngineCombo->addItems(QStringList()
        << QStringLiteral("Vulkan")
        << QStringLiteral("OpenGL"));
    m_backendEngineCombo->setCurrentIndex(0);
    engineLayout->addWidget(engineLabel);
    engineLayout->addWidget(m_backendEngineCombo);
    toolbar->addWidget(engineHost);
}

// 创建坐标状态栏。
void MainWindow::SetupStatusBar() {
    statusBar()->setSizeGripEnabled(false);

    m_frameClock.start();

    m_backendLabel = new QLabel();
    m_coordX = new QLabel();
    m_coordY = new QLabel();
    m_coordZ = new QLabel();
    m_fpsLabel = new QLabel(QStringLiteral("FPS: —"));
    m_frameTimeLabel = new QLabel(QStringLiteral("帧时间: —"));

    statusBar()->addPermanentWidget(m_backendLabel);
    statusBar()->addPermanentWidget(m_coordX);
    statusBar()->addPermanentWidget(m_coordY);
    statusBar()->addPermanentWidget(m_coordZ);
    statusBar()->addPermanentWidget(m_fpsLabel);
    statusBar()->addPermanentWidget(m_frameTimeLabel);
    UpdateBackendStatus();
}

// 刷新状态栏后端名称，并清空上次世界坐标。
void MainWindow::UpdateBackendStatus() {
    if (m_backendLabel) {
        m_backendLabel->setText(IsOpenGLBackend()
            ? QStringLiteral("后端: OpenGL")
            : QStringLiteral("后端: Vulkan（主）"));
    }
    if (m_coordX) m_coordX->setText(QStringLiteral("X: —"));
    if (m_coordY) m_coordY->setText(QStringLiteral("Y: —"));
    if (m_coordZ) m_coordZ->setText(QStringLiteral("Z: —"));
    // 切换后端/2D-3D 后让阴影通道立即重画一次。
    m_lastShadowPassTimer.invalidate();
}

// 刷新 FPS 与 CPU 帧时间标签。
void MainWindow::UpdateFrameStats(bool drewFrame, bool paused,
                                  qint64 tickStartNs, qint64 tickEndNs) {
    if (!m_fpsLabel || !m_frameTimeLabel) return;

    if (paused) {
        // 暂停不参与帧间隔，恢复后第一拍重新起算。
        m_hasLastDrawnFrame = false;
    }

    // 数值仍按每拍瞬时计算，文字按 kStatsRefreshMs 节流写入。
    QString fpsText;
    QString frameTimeText;
    if (paused) {
        fpsText = QStringLiteral("FPS: —");
        frameTimeText = QStringLiteral("帧时间: —");
    } else {
        // CPU 帧时间覆盖整个 tick：定时器触发到提交结束。
        const qint64 tickNs = tickEndNs - tickStartNs;
        frameTimeText = QStringLiteral("帧时间: %1 ms")
            .arg(static_cast<double>(tickNs) / 1.0e6, 0, 'f', 1);

        // 瞬时 FPS：1000 / 距上一张已画帧的间隔。
        if (m_hasLastDrawnFrame && tickEndNs > m_lastDrawnNs) {
            const double intervalNs = static_cast<double>(tickEndNs - m_lastDrawnNs);
            fpsText = QStringLiteral("FPS: %1").arg(1.0e9 / intervalNs, 0, 'f', 1);
        } else {
            fpsText = QStringLiteral("FPS: —");
        }
    }

    if (drewFrame) {
        m_lastDrawnNs = tickEndNs;
        m_hasLastDrawnFrame = true;
    }

    const bool pausedChanged = (paused != m_statsPausedShown);
    if (!pausedChanged && (tickEndNs - m_lastStatsTextNs) < kStatsRefreshMs * 1000000LL) {
        return;
    }
    m_lastStatsTextNs = tickEndNs;
    m_statsPausedShown = paused;
    m_fpsLabel->setText(fpsText);
    m_frameTimeLabel->setText(frameTimeText);
}

// 创建 Vulkan 窗口容器并接渲染循环。
void MainWindow::SetupVulkan() {
    auto* vkRenderer = dynamic_cast<VKRender*>(m_renderer);
    if (!vkRenderer) return;
    m_vulkanWindow = new QWindowVulkan(vkRenderer);
    m_container = QWidget::createWindowContainer(m_vulkanWindow);
    m_container->setMinimumSize(400, 300);
    m_container->setFocusPolicy(Qt::StrongFocus);
    m_vulkanWindow->installEventFilter(this);

    m_projectPanel = new QTreeWidget();
    if (QStyle* treeStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        treeStyle->setParent(m_projectPanel);
        m_projectPanel->setStyle(treeStyle);
    }
    m_projectPanel->setHeaderLabel(QStringLiteral("主图层"));
    m_projectPanel->setMinimumWidth(150);
    m_projectPanel->setContextMenuPolicy(Qt::CustomContextMenu);
    m_projectPanel->setRootIsDecorated(true);
    m_projectPanel->setItemsExpandable(true);
    m_projectPanel->setExpandsOnDoubleClick(false);
    m_projectPanel->setIndentation(18);
    // 任务 2.3：拦截项目树 viewport 的鼠标事件，用于“点击空白处取消选择”。
    m_projectPanel->viewport()->installEventFilter(this);

    m_modelsTreeItem = new QTreeWidgetItem(m_projectPanel);
    m_modelsTreeItem->setText(0, QStringLiteral("模型"));
    m_modelsTreeItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    m_modelsTreeItem->setExpanded(true);

    connect(m_projectPanel, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_projectPanel->itemAt(pos);
        if (!item) return;

        QMenu menu(m_projectPanel);
        if (item == m_modelsTreeItem) {
            QAction* clearAction = menu.addAction(QStringLiteral("清空"));
            if (menu.exec(m_projectPanel->viewport()->mapToGlobal(pos)) == clearAction) {
                ClearLoadedModels();
            }
            return;
        }

        // 模型项（“模型”根的直接子项）：显示 / 隐藏 / 移除。
        if (item->parent() == m_modelsTreeItem) {
            QAction* showAction = menu.addAction(QStringLiteral("显示"));
            QAction* hideAction = menu.addAction(QStringLiteral("隐藏"));
            QAction* removeAction = menu.addAction(QStringLiteral("移除"));
            QAction* selectedAction = menu.exec(m_projectPanel->viewport()->mapToGlobal(pos));
            if (selectedAction == showAction) {
                SetLoadedModelVisible(item, true);
            } else if (selectedAction == hideAction) {
                SetLoadedModelVisible(item, false);
            } else if (selectedAction == removeAction) {
                RemoveLoadedModel(item);
            }
            return;
        }

        // 节点项（任务 2.1）：重置变换 / 复制 / 删除。
        QAction* resetAction = menu.addAction(QStringLiteral("重置变换"));
        QAction* copyAction = menu.addAction(QStringLiteral("复制"));
        QAction* deleteAction = menu.addAction(QStringLiteral("删除"));
        QAction* selectedAction = menu.exec(m_projectPanel->viewport()->mapToGlobal(pos));
        if (selectedAction == resetAction) {
            m_projectPanel->setCurrentItem(item);
            ResetSelectedNodeTransform();
        } else if (selectedAction == copyAction) {
            m_projectPanel->setCurrentItem(item);
            CopySelectedNode();
        } else if (selectedAction == deleteAction) {
            m_projectPanel->setCurrentItem(item);
            DeleteSelectedNode();
        }
    });

    connect(m_projectPanel, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int /*column*/) {
        if (!item) return;

        if (item == m_modelsTreeItem) {
            FocusSceneOrModel(nullptr);
            return;
        }

        // 模型项：取景整个模型。
        if (item->parent() == m_modelsTreeItem) {
            FocusSceneOrModel(item);
        }
    });

    connect(m_projectPanel, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::OnProjectSelectionChanged);

    m_lightAnalysisPanel = new QWidget();
    m_lightAnalysisPanel->setFixedWidth(250);
    m_lightAnalysisPanel->setAutoFillBackground(true);
    m_lightAnalysisPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));

    QVBoxLayout* pTopLayout = new QVBoxLayout(m_lightAnalysisPanel);
    pTopLayout->setContentsMargins(0, 0, 0, 0);
    pTopLayout->setAlignment(Qt::AlignTop);

    QFormLayout* pFormLayout = new QFormLayout;
    pTopLayout->addLayout(pFormLayout);

    m_pComboTexSize = new QComboBox;
    m_pComboTexSize->addItems(QStringList() << QStringLiteral("1024") << QStringLiteral("2048")
                                           << QStringLiteral("4096") << QStringLiteral("8192"));
    m_pComboTexSize->setCurrentIndex(1);
    pFormLayout->addRow(QStringLiteral("阴影纹理大小"), m_pComboTexSize);

    m_pLatitudeEdit = new QLineEdit(QStringLiteral("36"), m_lightAnalysisPanel);
    pFormLayout->addRow(QStringLiteral("纬度"), m_pLatitudeEdit);

    connect(m_pComboTexSize, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        ApplyLightAnalysisToRenderer();
    });
    connect(m_pLatitudeEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        ApplyLightAnalysisToRenderer();
    });

    m_pDateEdit = new QDateEdit(QDate::currentDate(), m_lightAnalysisPanel);
    m_pDateEdit->setDateRange(QDate::currentDate().addYears(-20), QDate::currentDate().addYears(50));
    m_pDateEdit->setCalendarPopup(true);
    pFormLayout->addRow(QStringLiteral("日期"), m_pDateEdit);
    connect(m_pDateEdit, &QDateEdit::dateChanged, this, [this](const QDate&) {
        ApplyLightAnalysisToRenderer();
    });

    m_pTimeEdit = new QTimeEdit(QTime(12, 0), m_lightAnalysisPanel);
    m_pTimeEdit->setTimeRange(QTime(6, 0), QTime(18, 0));
    m_pTimeEdit->setDisplayFormat(QStringLiteral("H:mm"));
    pFormLayout->addRow(QStringLiteral("时间"), m_pTimeEdit);

    m_pTimeDial = new QDial(m_lightAnalysisPanel);
    m_pTimeDial->setFocusPolicy(Qt::StrongFocus);
    m_pTimeDial->setMinimum(0);
    m_pTimeDial->setMaximum(720);
    m_pTimeDial->setPageStep(12);
    m_pTimeDial->setNotchTarget(15);
    m_pTimeDial->setNotchesVisible(true);
    m_pTimeDial->setValue((m_pTimeEdit->time().hour() - 6) * 60 + m_pTimeEdit->time().minute());
    pTopLayout->addWidget(m_pTimeDial, 1, Qt::AlignHCenter);

    connect(m_pTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime& t) {
        const int v = (t.hour() - 6) * 60 + t.minute();
        if (m_pTimeDial->value() != v) {
            const QSignalBlocker blocker(m_pTimeDial);
            m_pTimeDial->setValue(v);
        }
        ApplyLightAnalysisToRenderer();
    });
    connect(m_pTimeDial, &QDial::valueChanged, this, [this](int v) {
        const QTime t = QTime(6, 0).addSecs(v * 60);
        if (m_pTimeEdit->time() != t) {
            const QSignalBlocker blocker(m_pTimeEdit);
            m_pTimeEdit->setTime(t);
        }
        ApplyLightAnalysisToRenderer();
    });

    m_lightAnalysisPanel->setVisible(false);
    ApplyLightAnalysisToRenderer();

    // 1.7：阴影偏移 / PCF / 法线偏移（追加到光照分析面板）。
    m_pShadowBiasSlider = new QSlider(Qt::Horizontal, m_lightAnalysisPanel);
    // 滑块 0..50 映射 0.000..0.005（步长 0.0001）。
    m_pShadowBiasSlider->setRange(0, 50);
    m_pShadowBiasSlider->setValue(20);  // 0.002（旧默认值）
    pFormLayout->addRow(QStringLiteral("阴影偏移"), m_pShadowBiasSlider);

    m_pPcfCombo = new QComboBox(m_lightAnalysisPanel);
    m_pPcfCombo->addItems(QStringList() << QStringLiteral("关闭")
                                        << QStringLiteral("3x3")
                                        << QStringLiteral("5x5"));
    m_pPcfCombo->setCurrentIndex(1);  // 3x3（旧默认）
    pFormLayout->addRow(QStringLiteral("PCF"), m_pPcfCombo);

    m_pNormalOffsetSlider = new QSlider(Qt::Horizontal, m_lightAnalysisPanel);
    // 滑块 0..50 映射 0.0..5.0 世界纹素倍数（步长 0.1）。
    m_pNormalOffsetSlider->setRange(0, 50);
    m_pNormalOffsetSlider->setValue(10);  // 1.0
    pFormLayout->addRow(QStringLiteral("法线偏移"), m_pNormalOffsetSlider);

    // 1.7：半球环境光（天空色 / 地面色 / 强度）。
    m_pSkyColorButton = new QPushButton(m_lightAnalysisPanel);
    pFormLayout->addRow(QStringLiteral("天空色"), m_pSkyColorButton);
    m_pGroundColorButton = new QPushButton(m_lightAnalysisPanel);
    pFormLayout->addRow(QStringLiteral("地面色"), m_pGroundColorButton);
    m_pAmbientIntensitySlider = new QSlider(Qt::Horizontal, m_lightAnalysisPanel);
    // 滑块 0..300 映射 0.0..3.0 强度（步长 0.01）。
    m_pAmbientIntensitySlider->setRange(0, 300);
    m_pAmbientIntensitySlider->setValue(100);  // 1.0
    pFormLayout->addRow(QStringLiteral("环境光强度"), m_pAmbientIntensitySlider);

    // 环境光颜色按钮：用线性色初始化（默认天空 0.03/0.035/0.045、地面 0.015/0.013/0.011）。
    m_ambientSkyColor = QColor::fromRgbF(0.03, 0.035, 0.045);
    m_ambientGroundColor = QColor::fromRgbF(0.015, 0.013, 0.011);
    auto updateColorButtons = [this]() {
        if (m_pSkyColorButton) {
            m_pSkyColorButton->setStyleSheet(QStringLiteral("background-color: %1;")
                .arg(m_ambientSkyColor.name()));
        }
        if (m_pGroundColorButton) {
            m_pGroundColorButton->setStyleSheet(QStringLiteral("background-color: %1;")
                .arg(m_ambientGroundColor.name()));
        }
    };
    updateColorButtons();
    connect(m_pSkyColorButton, &QPushButton::clicked, this, [this, updateColorButtons]() {
        const QColor c = QColorDialog::getColor(m_ambientSkyColor, this, QStringLiteral("天空色"));
        if (c.isValid()) {
            m_ambientSkyColor = c;
            updateColorButtons();
            ApplyLightAnalysisToRenderer();
        }
    });
    connect(m_pGroundColorButton, &QPushButton::clicked, this, [this, updateColorButtons]() {
        const QColor c = QColorDialog::getColor(m_ambientGroundColor, this, QStringLiteral("地面色"));
        if (c.isValid()) {
            m_ambientGroundColor = c;
            updateColorButtons();
            ApplyLightAnalysisToRenderer();
        }
    });
    connect(m_pShadowBiasSlider, &QSlider::valueChanged, this, [this](int) { ApplyLightAnalysisToRenderer(); });
    connect(m_pPcfCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { ApplyLightAnalysisToRenderer(); });
    connect(m_pNormalOffsetSlider, &QSlider::valueChanged, this, [this](int) { ApplyLightAnalysisToRenderer(); });
    connect(m_pAmbientIntensitySlider, &QSlider::valueChanged, this, [this](int) { ApplyLightAnalysisToRenderer(); });

    // PBR 调试面板（1.5）：金属度 / 粗糙度 / 自发光覆盖。默认不勾选，跟随材质自带值。
    m_pbrPanel = new QWidget();
    m_pbrPanel->setFixedWidth(250);
    m_pbrPanel->setAutoFillBackground(true);
    m_pbrPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));
    QVBoxLayout* pbrLayout = new QVBoxLayout(m_pbrPanel);
    pbrLayout->setContentsMargins(8, 6, 8, 6);
    pbrLayout->setSpacing(6);
    pbrLayout->setAlignment(Qt::AlignTop);

    auto addSliderRow = [&](const QString& title, QCheckBox*& check, QSlider*& slider,
                            int maxValue) {
        check = new QCheckBox(title);
        check->setChecked(false);
        pbrLayout->addWidget(check);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, maxValue);
        slider->setEnabled(false);
        pbrLayout->addWidget(slider);
        connect(check, &QCheckBox::toggled, slider, &QSlider::setEnabled);
    };
    // 金属度/粗糙度：0..100 映射 0.0..1.0；自发光倍率：0..500 映射 0.0..5.0。
    addSliderRow(QStringLiteral("金属度覆盖 (0-1)"), m_metallicOverrideCheck,
                 m_metallicSlider, 100);
    m_metallicSlider->setValue(0);
    addSliderRow(QStringLiteral("粗糙度覆盖 (0-1)"), m_roughnessOverrideCheck,
                 m_roughnessSlider, 100);
    m_roughnessSlider->setValue(50);
    addSliderRow(QStringLiteral("自发光倍率覆盖 (0-5)"), m_emissiveOverrideCheck,
                 m_emissiveSlider, 500);
    m_emissiveSlider->setValue(0);

    auto applyPbr = [this]() { ApplyPbrToRenderer(); };
    connect(m_metallicOverrideCheck, &QCheckBox::toggled, this, [applyPbr](bool) { applyPbr(); });
    connect(m_metallicSlider, &QSlider::valueChanged, this, [applyPbr](int) { applyPbr(); });
    connect(m_roughnessOverrideCheck, &QCheckBox::toggled, this, [applyPbr](bool) { applyPbr(); });
    connect(m_roughnessSlider, &QSlider::valueChanged, this, [applyPbr](int) { applyPbr(); });
    connect(m_emissiveOverrideCheck, &QCheckBox::toggled, this, [applyPbr](bool) { applyPbr(); });
    connect(m_emissiveSlider, &QSlider::valueChanged, this, [applyPbr](int) { applyPbr(); });

    m_pbrPanel->setVisible(false);

    // HDR/曝光面板（1.6）：HDR 开关 + 曝光(EV) 滑块。默认关闭 HDR，回退到旧 LDR 画面。
    m_hdrPanel = new QWidget();
    m_hdrPanel->setFixedWidth(250);
    m_hdrPanel->setAutoFillBackground(true);
    m_hdrPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));
    QVBoxLayout* hdrLayout = new QVBoxLayout(m_hdrPanel);
    hdrLayout->setContentsMargins(8, 6, 8, 6);
    hdrLayout->setSpacing(6);
    hdrLayout->setAlignment(Qt::AlignTop);

    m_hdrCheck = new QCheckBox(QStringLiteral("启用 HDR + ACES 色调映射"));
    m_hdrCheck->setChecked(false);
    hdrLayout->addWidget(m_hdrCheck);

    m_exposureValueLabel = new QLabel(QStringLiteral("曝光: 0.0 EV"));
    m_exposureValueLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    hdrLayout->addWidget(m_exposureValueLabel);

    m_exposureSlider = new QSlider(Qt::Horizontal);
    // EV 档位 -5..+5，步长 0.1（滑块整数 0..100 映射 -5..+5）。
    m_exposureSlider->setRange(0, 100);
    m_exposureSlider->setValue(50);
    hdrLayout->addWidget(m_exposureSlider);

    auto applyHdr = [this]() { ApplyHdrToRenderer(); };
    connect(m_hdrCheck, &QCheckBox::toggled, this, [applyHdr](bool) { applyHdr(); });
    connect(m_exposureSlider, &QSlider::valueChanged, this, [applyHdr](int) { applyHdr(); });

    m_hdrPanel->setVisible(false);

    // 调试视图面板（1.7）：调试视图下拉 + 4x MSAA 开关。
    m_debugPanel = new QWidget();
    m_debugPanel->setFixedWidth(250);
    m_debugPanel->setAutoFillBackground(true);
    m_debugPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));
    QVBoxLayout* debugLayout = new QVBoxLayout(m_debugPanel);
    debugLayout->setContentsMargins(8, 6, 8, 6);
    debugLayout->setSpacing(6);
    debugLayout->setAlignment(Qt::AlignTop);

    QLabel* debugViewLabel = new QLabel(QStringLiteral("调试视图"));
    debugViewLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    debugLayout->addWidget(debugViewLabel);
    m_debugViewCombo = new QComboBox();
    m_debugViewCombo->addItems(QStringList()
        << QStringLiteral("正常")
        << QStringLiteral("深度")
        << QStringLiteral("世界法线")
        << QStringLiteral("阴影贴图"));
    m_debugViewCombo->setCurrentIndex(0);
    debugLayout->addWidget(m_debugViewCombo);

    m_msaaCheck = new QCheckBox(QStringLiteral("启用 4x MSAA"));
    m_msaaCheck->setChecked(false);
    debugLayout->addWidget(m_msaaCheck);

    m_debugHintLabel = new QLabel(QString());
    m_debugHintLabel->setWordWrap(true);
    m_debugHintLabel->setStyleSheet(QStringLiteral("color: #c8a000; border: none;"));
    m_debugHintLabel->setVisible(false);
    debugLayout->addWidget(m_debugHintLabel);

    auto applyDebug = [this]() { ApplyDebugToRenderer(); };
    connect(m_debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [applyDebug](int) { applyDebug(); });
    connect(m_msaaCheck, &QCheckBox::toggled, this, [applyDebug](bool) { applyDebug(); });

    m_debugPanel->setVisible(false);

    // 变换编辑面板（任务 2.1）：平移 / 旋转（度）/ 缩放 数值输入。
    m_transformPanel = new QWidget();
    m_transformPanel->setFixedWidth(250);
    m_transformPanel->setAutoFillBackground(true);
    m_transformPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));
    QVBoxLayout* transformLayout = new QVBoxLayout(m_transformPanel);
    transformLayout->setContentsMargins(8, 6, 8, 6);
    transformLayout->setSpacing(6);
    transformLayout->setAlignment(Qt::AlignTop);

    m_transformSelectionLabel = new QLabel(QStringLiteral("未选中节点"));
    m_transformSelectionLabel->setWordWrap(true);
    m_transformSelectionLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    transformLayout->addWidget(m_transformSelectionLabel);

    // 三行三列数值：平移 / 旋转 / 缩放 的 X/Y/Z。
    auto makeSpinRow = [&](const QString& title, QDoubleSpinBox*& x,
                           QDoubleSpinBox*& y, QDoubleSpinBox*& z,
                           double minValue, double maxValue, double step, int decimals) {
        QLabel* rowLabel = new QLabel(title);
        rowLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
        transformLayout->addWidget(rowLabel);
        QHBoxLayout* row = new QHBoxLayout();
        row->setSpacing(4);
        auto makeSpin = [&](QDoubleSpinBox*& spin) {
            spin = new QDoubleSpinBox();
            spin->setRange(minValue, maxValue);
            spin->setSingleStep(step);
            spin->setDecimals(decimals);
            spin->setValue(0.0);
            row->addWidget(spin);
        };
        makeSpin(x);
        makeSpin(y);
        makeSpin(z);
        transformLayout->addLayout(row);
    };
    makeSpinRow(QStringLiteral("平移 (X/Y/Z)"), m_spinTransX, m_spinTransY, m_spinTransZ,
                -1.0e6, 1.0e6, 0.1, 4);
    makeSpinRow(QStringLiteral("旋转 (度 X/Y/Z)"), m_spinRotX, m_spinRotY, m_spinRotZ,
                -360.0, 360.0, 5.0, 2);
    makeSpinRow(QStringLiteral("缩放 (X/Y/Z)"), m_spinScaleX, m_spinScaleY, m_spinScaleZ,
                0.0001, 1.0e6, 0.1, 4);
    if (m_spinScaleX) m_spinScaleX->setValue(1.0);
    if (m_spinScaleY) m_spinScaleY->setValue(1.0);
    if (m_spinScaleZ) m_spinScaleZ->setValue(1.0);

    QPushButton* transformResetButton = new QPushButton(QStringLiteral("重置变换"));
    transformLayout->addWidget(transformResetButton);
    connect(transformResetButton, &QPushButton::clicked, this,
            &MainWindow::ResetSelectedNodeTransform);

    // 任务 2.4：Gizmo 模式选择器（平移 / 旋转 / 缩放）+ 坐标空间（默认局部）+ 吸附步长。
    QLabel* gizmoModeLabel = new QLabel(QStringLiteral("Gizmo 模式"));
    gizmoModeLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    transformLayout->addWidget(gizmoModeLabel);
    m_gizmoModeCombo = new QComboBox();
    m_gizmoModeCombo->addItems(QStringList()
        << QStringLiteral("平移")
        << QStringLiteral("旋转")
        << QStringLiteral("缩放"));
    m_gizmoModeCombo->setCurrentIndex(0);
    transformLayout->addWidget(m_gizmoModeCombo);
    connect(m_gizmoModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        switch (index) {
        case 1: m_gizmoMode = GizmoMode::Rotate; break;
        case 2: m_gizmoMode = GizmoMode::Scale; break;
        default: m_gizmoMode = GizmoMode::Translate; break;
        }
        // 切换模式后取消悬停高亮，避免残留错误状态。
        m_gizmoHoverAxis = GizmoAxis::None;
    });

    m_gizmoWorldSpaceCheck = new QCheckBox(QStringLiteral("世界坐标模式"));
    m_gizmoWorldSpaceCheck->setLayoutDirection(Qt::LeftToRight);
    m_gizmoWorldSpaceCheck->setChecked(false);
    m_gizmoWorldSpaceCheck->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    transformLayout->addWidget(m_gizmoWorldSpaceCheck);
    connect(m_gizmoWorldSpaceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_gizmoWorldSpace = checked;
        // 切换空间后取消悬停高亮，避免残留错误状态。
        m_gizmoHoverAxis = GizmoAxis::None;
    });

    m_gizmoSnapCheck = new QCheckBox(QStringLiteral("吸附"));
    m_gizmoSnapCheck->setLayoutDirection(Qt::LeftToRight);
    m_gizmoSnapCheck->setChecked(false);
    m_gizmoSnapCheck->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    transformLayout->addWidget(m_gizmoSnapCheck);
    connect(m_gizmoSnapCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_gizmoSnapEnabled = checked;
    });

    auto makeSnapRow = [&](const QString& title, QDoubleSpinBox*& spin,
                           double minValue, double maxValue, double step, int decimals,
                           double initial) {
        QLabel* rowLabel = new QLabel(title);
        rowLabel->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
        transformLayout->addWidget(rowLabel);
        spin = new QDoubleSpinBox();
        spin->setRange(minValue, maxValue);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(initial);
        transformLayout->addWidget(spin);
    };
    makeSnapRow(QStringLiteral("吸附步长：平移"), m_gizmoSnapTranslate,
                0.001, 1.0e6, 0.5, 3, 1.0);
    makeSnapRow(QStringLiteral("吸附步长：旋转(度)"), m_gizmoSnapRotate,
                0.1, 360.0, 1.0, 2, 15.0);
    makeSnapRow(QStringLiteral("吸附步长：缩放"), m_gizmoSnapScale,
                0.001, 100.0, 0.05, 3, 0.1);

    auto onTransformSpinChanged = [this](double) { ApplyTransformPanelToSelection(); };
    for (QDoubleSpinBox* spin : { m_spinTransX, m_spinTransY, m_spinTransZ,
                                  m_spinRotX, m_spinRotY, m_spinRotZ,
                                  m_spinScaleX, m_spinScaleY, m_spinScaleZ }) {
        if (spin) {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, onTransformSpinChanged);
        }
    }

    m_transformPanel->setVisible(false);

    QWidget* viewportHost = new QWidget();
    QHBoxLayout* viewportLayout = new QHBoxLayout(viewportHost);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);
    m_viewportLayout = viewportLayout;
    viewportLayout->addWidget(m_container, 1);
    viewportLayout->addWidget(m_lightAnalysisPanel, 0);
    viewportLayout->addWidget(m_pbrPanel, 0);
    viewportLayout->addWidget(m_hdrPanel, 0);
    viewportLayout->addWidget(m_debugPanel, 0);
    viewportLayout->addWidget(m_transformPanel, 0);

    // 右栏：上方视口，下方消息区（projectPanel 右边、渲染视口下方、状态栏上方）。
    m_messageList = new QListWidget();
    if (QStyle* listStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        listStyle->setParent(m_messageList);
        m_messageList->setStyle(listStyle);
    }
    m_messageList->setWordWrap(true);
    m_messageList->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageList->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #252526; color: #d4d4d4; border: none; outline: none; }"
        "QListWidget::item { padding: 1px 6px; }"));

    m_messageSplitter = new QSplitter(Qt::Vertical);
    m_messageSplitter->setHandleWidth(1);
    m_messageSplitter->addWidget(viewportHost);

    // 材质面板：位于渲染窗口正下方、消息提示框正上方，横跨整个右栏。
    m_materialPanel = new QWidget();
    m_materialPanel->setAutoFillBackground(true);
    m_materialPanel->setStyleSheet(QStringLiteral("background-color: #252526; border: 1px solid #3c3c3c;"));
    QVBoxLayout* materialLayout = new QVBoxLayout(m_materialPanel);
    materialLayout->setContentsMargins(4, 2, 4, 2);
    materialLayout->setSpacing(2);
    QLabel* materialTitle = new QLabel(QStringLiteral("材质可见性"));
    materialTitle->setStyleSheet(QStringLiteral("color: #d4d4d4; font-size: 12px; border: none;"));
    materialLayout->addWidget(materialTitle);
    m_materialTree = new QTreeWidget();
    m_materialTree->setHeaderHidden(true);
    m_materialTree->setRootIsDecorated(true);
    m_materialTree->setIndentation(16);
    m_materialTree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background-color: #1e1e1e; color: #d4d4d4; border: none; outline: none; }"
        "QTreeWidget::item { padding: 1px 4px; }"
        "QTreeWidget::item:selected { background-color: #094771; }"));
    materialLayout->addWidget(m_materialTree);
    m_materialPanel->setVisible(false);

    // 材质勾选变化：应用到对应模型的网格（材质级或整模型组）。
    connect(m_materialTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
        if (!item || column != 0) return;
        const QVariant modelData = item->data(0, Qt::UserRole);
        if (!modelData.isValid()) return;
        const int modelIndex = modelData.toInt();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(m_loadedModels.size())) return;
        LoadedModel& model = m_loadedModels[static_cast<size_t>(modelIndex)];
        const bool visible = item->checkState(0) == Qt::Checked;
        const int materialIndex = item->data(0, Qt::UserRole + 1).toInt();

        if (materialIndex < 0) {
            // 组节点：把状态应用到该模型下所有材质。
            for (int index = 0;
                 index < static_cast<int>(model.asset.materials.size());
                 ++index) {
                model.materialVisible[index] = visible;
            }
            ApplyModelMaterialVisibility(model);
            // 同步子项勾选状态（阻止递归触发）。
            const QSignalBlocker blocker(m_materialTree);
            for (int child = 0; child < item->childCount(); ++child) {
                item->child(child)->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            }
            return;
        }

        // 材质节点：只切换该材质。
        model.materialVisible[materialIndex] = visible;
        ApplyModelMaterialVisibility(model);
    });

    m_messageSplitter->addWidget(m_materialPanel);

    m_messageSplitter->addWidget(m_messageList);
    m_messageSplitter->setStretchFactor(0, 1);
    m_messageSplitter->setStretchFactor(1, 0);
    m_messageSplitter->setStretchFactor(2, 0);
    m_messageSplitter->setSizes({ 600, 120, 120 });

    QWidget* rightHost = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightHost);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_messageSplitter);

    QSplitter* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setHandleWidth(1);
    hSplitter->addWidget(m_projectPanel);
    hSplitter->addWidget(rightHost);
    hSplitter->setStretchFactor(0, 1);
    hSplitter->setStretchFactor(1, 2);

    QWidget* central = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(hSplitter);
    setCentralWidget(central);

    connect(m_vulkanWindow, &QWindowVulkan::vulkanReady, this, [this]() {
        if (!m_renderer || !m_renderer->IsInitialized()) {
            ReportRendererError(m_renderer, QStringLiteral("Vulkan 初始化失败"));
            return;
        }
        StartRenderLoop();
    });
}

// 把渲染器初始化或着色器加载失败写入消息区。
void MainWindow::ReportRendererError(Render* renderer, const QString& stage) {
    QString message = stage;
    if (renderer && !renderer->GetLastError().empty()) {
        message += QStringLiteral("：") + QString::fromStdString(renderer->GetLastError());
    } else {
        message += QStringLiteral("：未返回具体原因。请确认可执行文件旁的 shaders 目录中存在所需着色器。");
    }
    ShowMessage(message, true);
}

// 在消息区追加一条提示（isError 用红色）。
void MainWindow::ShowMessage(const QString& text, bool isError) {
    if (!m_messageList) return;
    QListWidgetItem* item = new QListWidgetItem(text);
    if (isError) {
        item->setForeground(QColor(0xF4, 0x87, 0x71));
    }
    m_messageList->addItem(item);
    m_messageList->scrollToBottom();
}

// 逐条追加显示导入诊断（不清空，消息区为只追加日志，跨多次导入累积）。
void MainWindow::ShowImportMessages(const std::vector<ImportMessage>& messages) {
    if (!m_messageList) return;
    for (const ImportMessage& message : messages) {
        ShowMessage(QString::fromStdString(message.message), message.isError);
    }
}

// 设备丢失时停循环并提示重启。
void MainWindow::HandleDeviceLost(Render* renderer) {
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    ReportRendererError(renderer, QStringLiteral("图形设备已丢失"));
}

// 把鼠标/滚轮事件转给当前渲染器。
bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // 任务 2.3：项目树 viewport 的空白处点击 -> 取消选择。
    if (m_projectPanel && obj == m_projectPanel->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton &&
                !m_projectPanel->itemAt(me->pos())) {
                ClearSelectionByEmptyClick();
            }
        }
        return QMainWindow::eventFilter(obj, event);
    }

    QWindow* window = nullptr;
    if (obj == m_vulkanWindow) window = m_vulkanWindow;
    else if (obj == m_openglWindow) window = m_openglWindow;
    if (!window) return QMainWindow::eventFilter(obj, event);

    const bool isOpenGL = (obj == m_openglWindow);
    auto dispatchDown = [&](float nx, float ny, int btn) {
        if (isOpenGL) {
            if (m_openglRenderer) m_openglRenderer->OnMouseDown(nx, ny, btn);
        } else if (m_renderer) {
            m_renderer->OnMouseDown(nx, ny, btn);
        }
    };
    auto dispatchUp = [&](int btn) {
        if (isOpenGL) {
            if (m_openglRenderer) m_openglRenderer->OnMouseUp(btn);
        } else if (m_renderer) {
            m_renderer->OnMouseUp(btn);
        }
    };
    auto dispatchMove = [&](float nx, float ny) {
        if (isOpenGL) {
            if (m_openglRenderer) m_openglRenderer->OnMouseMove(nx, ny);
            if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
                render3D->RequestCoordReadback(nx, ny);
            }
        } else if (m_renderer) {
            m_renderer->OnMouseMove(nx, ny);
            if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
                render3D->RequestCoordReadback(nx, ny);
            }
        }
    };
    auto dispatchWheel = [&](float delta) {
        if (isOpenGL) {
            if (m_openglRenderer) m_openglRenderer->OnMouseWheel(delta);
        } else if (m_renderer) {
            m_renderer->OnMouseWheel(delta);
        }
    };

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(event);
        float nx = (float)me->x() / (float)window->width();
        float ny = (float)me->y() / (float)window->height();
        int btn = -1;
        if (me->button() == Qt::LeftButton) btn = 0;
        else if (me->button() == Qt::RightButton) btn = 1;
        else if (me->button() == Qt::MiddleButton) btn = 2;
        // 任务 2.4：左键优先尝试抓取 Gizmo 轴；命中则消费事件，不再转给相机。
        if (btn == 0 && TryBeginGizmoDrag(nx, ny)) {
            break;
        }
        dispatchDown(nx, ny, btn);
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(event);
        int btn = -1;
        if (me->button() == Qt::LeftButton) btn = 0;
        else if (me->button() == Qt::RightButton) btn = 1;
        else if (me->button() == Qt::MiddleButton) btn = 2;
        // 任务 2.4：结束 Gizmo 拖拽（若有）。
        if (btn == 0 && IsGizmoDragging()) {
            EndGizmoDrag();
            break;
        }
        dispatchUp(btn);
        break;
    }
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);
        float nx = (float)me->x() / (float)window->width();
        float ny = (float)me->y() / (float)window->height();
        // 任务 2.4：Gizmo 拖拽中则只更新 Gizmo，不转给相机。
        if (TryUpdateGizmoDrag(nx, ny)) {
            break;
        }
        // 非拖拽时更新 Gizmo 悬停高亮（不影响相机 / 回读）。
        UpdateGizmoHover(nx, ny);
        dispatchMove(nx, ny);
        break;
    }
    case QEvent::Wheel: {
        auto* we = static_cast<QWheelEvent*>(event);
        dispatchWheel((float)we->angleDelta().y());
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, event);
}

// 启动渲染帧定时器（间隔 kRenderTickMs，不封顶节拍）。
void MainWindow::StartRenderLoop() {
    if (m_renderTimer) {
        m_renderTimer->start(kRenderTickMs);
        return;
    }
    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, [this]() {
        const qint64 tickStartNs = m_frameClock.nsecsElapsed();
        const bool openGL = IsOpenGLBackend();
        Render* activeRenderer = openGL ? m_openglRenderer : m_renderer;
        QWindow* activeWindow = openGL
            ? static_cast<QWindow*>(m_openglWindow)
            : static_cast<QWindow*>(m_vulkanWindow);
        const bool paused = !activeRenderer || !activeRenderer->IsInitialized()
            || activeRenderer->IsDeviceLost()
            || !activeWindow || activeWindow->width() == 0 || activeWindow->height() == 0;
        bool drewFrame = false;
        // 节拍快于阴影重画间隔时，本拍跳过阴影通道，复用现有阴影贴图。
        const bool shadowDue = !m_lastShadowPassTimer.isValid()
            || m_lastShadowPassTimer.elapsed() >= kShadowPassIntervalMs;

        // 任务 2.1：按脏标记刷新场景图世界矩阵（变换编辑后每帧传播一次）。
        if (!paused && m_scene) {
            m_scene->UpdateWorldTransforms(nullptr, false);
        }

        // 任务 2.4：把当前 Gizmo 线段几何提交给活动渲染器（本帧叠加层绘制用）。
        if (!paused) {
            UpdateGizmoGeometry();
        }

        if (openGL) {
            if (!paused) {
                if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
                    if (shadowDue && m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible() &&
                        render3D->IsSunAboveHorizon()) {
                        if (render3D->BeginShadowPass()) {
                            if (m_scene) m_scene->Render(Object::RM_SHADOW);
                            render3D->EndShadowPass();
                            m_lastShadowPassTimer.start();
                        }
                    }
                }
                if (m_openglRenderer->BeginFrame()) {
                    if (m_scene) m_scene->Render(0);
                    m_openglRenderer->EndFrame();
                    drewFrame = true;
                }
                if (m_openglRenderer->IsDeviceLost()) {
                    HandleDeviceLost(m_openglRenderer);
                    UpdateFrameStats(false, true, tickStartNs, m_frameClock.nsecsElapsed());
                    return;
                }
                if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
                    if (render3D->HasNewWorldCoord()) {
                        m_coordX->setText(QStringLiteral("X: %1").arg(render3D->GetLastWorldX(), 0, 'f', 3));
                        m_coordY->setText(QStringLiteral("Y: %1").arg(render3D->GetLastWorldY(), 0, 'f', 3));
                        m_coordZ->setText(QStringLiteral("Z: %1").arg(render3D->GetLastWorldZ(), 0, 'f', 3));
                    }
                }
            }
            UpdateFrameStats(drewFrame, paused, tickStartNs, m_frameClock.nsecsElapsed());
            return;
        }

        if (!paused) {
            if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
                if (shadowDue && m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible() &&
                    render3D->IsSunAboveHorizon()) {
                    if (render3D->BeginShadowPass()) {
                        if (m_scene) m_scene->Render(Object::RM_SHADOW);
                        render3D->EndShadowPass();
                        m_lastShadowPassTimer.start();
                    }
                }
            }
            if (m_renderer->BeginFrame()) {
                m_scene->Render(0);
                m_renderer->EndFrame();
                drewFrame = true;
            }
            if (m_renderer->IsDeviceLost()) {
                HandleDeviceLost(m_renderer);
                UpdateFrameStats(false, true, tickStartNs, m_frameClock.nsecsElapsed());
                return;
            }
            if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
                if (render3D->HasNewWorldCoord()) {
                    m_coordX->setText(QStringLiteral("X: %1").arg(render3D->GetLastWorldX(), 0, 'f', 3));
                    m_coordY->setText(QStringLiteral("Y: %1").arg(render3D->GetLastWorldY(), 0, 'f', 3));
                    m_coordZ->setText(QStringLiteral("Z: %1").arg(render3D->GetLastWorldZ(), 0, 'f', 3));
                }
            }
        }
        UpdateFrameStats(drewFrame, paused, tickStartNs, m_frameClock.nsecsElapsed());
    });
    m_renderTimer->start(kRenderTickMs);
}

// 打开或关闭光照分析面板。
void MainWindow::onLightAnalysis() {
    if (!m_lightAnalysisPanel) return;
    const bool open = !m_lightAnalysisPanel->isVisible();
    m_lightAnalysisPanel->setVisible(open);
    if (m_lightAnalysisButton) {
        m_lightAnalysisButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    ApplyLightAnalysisToRenderer();
}

// 打开或关闭材质可见性面板。
void MainWindow::onMaterialPanel() {
    if (!m_materialPanel) return;
    const bool open = !m_materialPanel->isVisible();
    m_materialPanel->setVisible(open);
    if (m_materialButton) {
        m_materialButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    if (open) {
        RebuildMaterialPanel();
    }
}

// 打开或关闭 PBR 调试面板。
void MainWindow::onPbrPanel() {
    if (!m_pbrPanel) return;
    const bool open = !m_pbrPanel->isVisible();
    m_pbrPanel->setVisible(open);
    if (m_pbrButton) {
        m_pbrButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    ApplyPbrToRenderer();
}

// 把 PBR 调试面板参数写进当前三维渲染器。
void MainWindow::ApplyPbrToRenderer() {
    const bool metallicOn = m_metallicOverrideCheck && m_metallicOverrideCheck->isChecked();
    const float metallic = m_metallicSlider
        ? static_cast<float>(m_metallicSlider->value()) / 100.0f : 0.0f;
    const bool roughnessOn = m_roughnessOverrideCheck && m_roughnessOverrideCheck->isChecked();
    const float roughness = m_roughnessSlider
        ? static_cast<float>(m_roughnessSlider->value()) / 100.0f : 0.5f;
    const bool emissiveOn = m_emissiveOverrideCheck && m_emissiveOverrideCheck->isChecked();
    const float emissive = m_emissiveSlider
        ? static_cast<float>(m_emissiveSlider->value()) / 100.0f : 0.0f;

    auto apply = [&](auto* render3D) {
        if (!render3D) return;
        render3D->SetMetallicOverride(metallicOn, metallic);
        render3D->SetRoughnessOverride(roughnessOn, roughness);
        render3D->SetEmissiveOverride(emissiveOn, emissive);
    };
    apply(dynamic_cast<VKRender3D*>(m_renderer));
    apply(dynamic_cast<GLRender3D*>(m_openglRenderer));
}

// 打开或关闭 HDR/曝光面板。
void MainWindow::onHdrPanel() {
    if (!m_hdrPanel) return;
    const bool open = !m_hdrPanel->isVisible();
    m_hdrPanel->setVisible(open);
    if (m_hdrButton) {
        m_hdrButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    ApplyHdrToRenderer();
}

// 打开或关闭调试视图面板。
void MainWindow::onDebugPanel() {
    if (!m_debugPanel) return;
    const bool open = !m_debugPanel->isVisible();
    m_debugPanel->setVisible(open);
    if (m_debugButton) {
        m_debugButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    ApplyDebugToRenderer();
}

// 打开或关闭变换编辑面板（任务 2.1）。
void MainWindow::onTransformPanel() {
    if (!m_transformPanel) return;
    const bool open = !m_transformPanel->isVisible();
    m_transformPanel->setVisible(open);
    if (m_transformButton) {
        m_transformButton->setStyleSheet(open
            ? QStringLiteral("background-color: #0078d4; color: #ffffff;")
            : QString());
    }
    if (open) {
        SyncTransformPanelFromSelection();
    }
}

// 把调试视图面板参数写进当前三维渲染器。
void MainWindow::ApplyDebugToRenderer() {
    const int view = m_debugViewCombo ? m_debugViewCombo->currentIndex() : 0;
    const bool msaaOn = m_msaaCheck && m_msaaCheck->isChecked();

    auto apply = [&](auto* render3D) {
        if (!render3D) return;
        render3D->SetDebugView(view);
        render3D->SetMsaaEnabled(msaaOn);
    };
    apply(dynamic_cast<VKRender3D*>(m_renderer));
    apply(dynamic_cast<GLRender3D*>(m_openglRenderer));

    // “阴影贴图”调试视图需要先开启光照分析（否则无阴影贴图可显示）。
    if (m_debugHintLabel) {
        const bool needsLight = (view == 3)
            && !(m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible());
        m_debugHintLabel->setText(needsLight
            ? QStringLiteral("提示：请先开启“光照分析”以渲染阴影贴图")
            : QString());
        m_debugHintLabel->setVisible(needsLight);
    }
}

// 把 HDR/曝光面板参数写进当前三维渲染器。
void MainWindow::ApplyHdrToRenderer() {
    const bool hdrOn = m_hdrCheck && m_hdrCheck->isChecked();
    // 滑块 0..100 映射 EV -5..+5。
    const float ev = m_exposureSlider
        ? (static_cast<float>(m_exposureSlider->value()) - 50.0f) / 10.0f
        : 0.0f;
    if (m_exposureValueLabel) {
        m_exposureValueLabel->setText(QStringLiteral("曝光: %1 EV").arg(ev, 0, 'f', 1));
    }

    auto apply = [&](auto* render3D) {
        if (!render3D) return;
        render3D->SetHdrEnabled(hdrOn);
        render3D->SetExposureEV(ev);
    };
    apply(dynamic_cast<VKRender3D*>(m_renderer));
    apply(dynamic_cast<GLRender3D*>(m_openglRenderer));
}

// 重建材质面板内容（按模型分组 + 每材质复选框 + 组内总开关）。
void MainWindow::RebuildMaterialPanel() {
    if (!m_materialTree) return;
    // 重建期间屏蔽 itemChanged，避免勾选状态写入触发回调。
    const QSignalBlocker blocker(m_materialTree);
    m_materialTree->clear();

    for (size_t modelIndex = 0; modelIndex < m_loadedModels.size(); ++modelIndex) {
        LoadedModel& model = m_loadedModels[modelIndex];
        if (model.asset.materials.empty()) continue;

        const QString modelName = model.treeItem
            ? model.treeItem->text(0)
            : QFileInfo(QString::fromStdString(model.asset.sourcePath)).fileName();
        QTreeWidgetItem* groupItem = new QTreeWidgetItem(m_materialTree);
        groupItem->setText(0, modelName);
        groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable);
        groupItem->setExpanded(true);
        groupItem->setData(0, Qt::UserRole, static_cast<int>(modelIndex));
        groupItem->setData(0, Qt::UserRole + 1, -1);  // -1 = 组节点

        bool allVisible = true;
        for (int materialIndex = 0;
             materialIndex < static_cast<int>(model.asset.materials.size());
             ++materialIndex) {
            const Material& material = model.asset.materials[static_cast<size_t>(materialIndex)];
            const bool visible = model.materialVisible.count(materialIndex) == 0
                ? true
                : model.materialVisible[materialIndex];
            if (!visible) allVisible = false;

            QTreeWidgetItem* materialItem = new QTreeWidgetItem(groupItem);
            QString label = material.name.empty()
                ? QStringLiteral("材质 %1").arg(materialIndex)
                : QString::fromStdString(material.name);
            if (material.alphaMode == MaterialAlphaMode::Blend) {
                label += QStringLiteral("（透明）");
            } else if (material.alphaMode == MaterialAlphaMode::Mask) {
                label += QStringLiteral("（遮罩）");
            }
            materialItem->setText(0, label);
            materialItem->setFlags(materialItem->flags() | Qt::ItemIsUserCheckable);
            materialItem->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            materialItem->setData(0, Qt::UserRole, static_cast<int>(modelIndex));
            materialItem->setData(0, Qt::UserRole + 1, materialIndex);
        }
        groupItem->setCheckState(0, allVisible ? Qt::Checked : Qt::Unchecked);
    }
}

// 应用某模型的材质可见性到其网格对象。
void MainWindow::ApplyModelMaterialVisibility(LoadedModel& model) {
    for (Object* mesh : model.meshes) {
        if (!mesh) continue;
        for (const auto& entry : model.materialVisible) {
            if (auto* vkMesh = dynamic_cast<VKMesh*>(mesh)) {
                vkMesh->SetMaterialVisible(entry.first, entry.second);
            } else if (auto* glMesh = dynamic_cast<GLMesh*>(mesh)) {
                glMesh->SetMaterialVisible(entry.first, entry.second);
            }
        }
    }
}

// 把光照面板参数写进当前三维渲染器。
void MainWindow::ApplyLightAnalysisToRenderer() {    auto applySun = [this](auto* render3D) {
        if (!render3D) return;
        if (m_pLatitudeEdit) {
            bool ok = false;
            const float latitude = m_pLatitudeEdit->text().toFloat(&ok);
            if (ok) {
                render3D->SetLatitude(latitude);
            }
        }
        if (m_pDateEdit) {
            const QDate d = m_pDateEdit->date();
            render3D->SetLightDate(d.year(), d.month(), d.day());
        }
        if (m_pTimeEdit) {
            const QTime t = m_pTimeEdit->time();
            render3D->SetLightTimeMinutes((t.hour() - 6) * 60 + t.minute());
        }
    };
    // 1.7：阴影偏移 / PCF / 法线偏移 + 半球环境光（仅存参数，不分配资源）——两个后端都写入。
    auto applySharedParams = [this](auto* render3D) {
        if (!render3D) return;
        if (m_pShadowBiasSlider) {
            render3D->SetShadowBias(static_cast<float>(m_pShadowBiasSlider->value()) / 10000.0f);
        }
        if (m_pPcfCombo) {
            render3D->SetShadowPcfMode(m_pPcfCombo->currentIndex());
        }
        if (m_pNormalOffsetSlider) {
            render3D->SetShadowNormalOffsetScale(static_cast<float>(m_pNormalOffsetSlider->value()) / 10.0f);
        }
        if (m_pAmbientIntensitySlider) {
            const Vec3 sky = { static_cast<float>(m_ambientSkyColor.redF()),
                               static_cast<float>(m_ambientSkyColor.greenF()),
                               static_cast<float>(m_ambientSkyColor.blueF()) };
            const Vec3 ground = { static_cast<float>(m_ambientGroundColor.redF()),
                                  static_cast<float>(m_ambientGroundColor.greenF()),
                                  static_cast<float>(m_ambientGroundColor.blueF()) };
            render3D->SetAmbientLight(sky, ground,
                static_cast<float>(m_pAmbientIntensitySlider->value()) / 100.0f);
        }
    };
    // 阴影资源相关（会分配阴影贴图）：只应用到当前活动后端。
    auto applyShadowState = [this](auto* render3D) {
        if (!render3D) return;
        render3D->SetLightAnalysisEnabled(m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible());
        if (m_pComboTexSize) {
            const uint32_t size = m_pComboTexSize->currentText().toUInt();
            if (size == 1024 || size == 2048 || size == 4096 || size == 8192) {
                render3D->SetShadowTextureSize(size);
            }
        }
        SyncShadowTextureSizeCombo(render3D->GetShadowTextureSize());
        ShowShadowMapStatus(render3D->TakeShadowMapStatus());
    };

    auto* vulkan3D = dynamic_cast<VKRender3D*>(m_renderer);
    auto* opengl3D = dynamic_cast<GLRender3D*>(m_openglRenderer);
    applySun(vulkan3D);
    applySun(opengl3D);
    applySharedParams(vulkan3D);
    applySharedParams(opengl3D);
    if (IsOpenGLBackend()) {
        applyShadowState(opengl3D);
    } else {
        applyShadowState(vulkan3D);
    }
    UpdateShadowSceneBounds();
    // 光照参数变化后让阴影通道立即重画一次，锁定新的光源矩阵。
    m_lastShadowPassTimer.invalidate();
}

// 用已加载模型包围盒更新阴影范围（含节点变换，归一化场景空间）。
void MainWindow::UpdateShadowSceneBounds() {
    Vec3 bboxMin = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    Vec3 bboxMax = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };
    bool anyVisible = false;

    for (const LoadedModel& model : m_loadedModels) {
        if (!model.visible) continue;
        AccumulateModelBounds(model, bboxMin, bboxMax, anyVisible);
    }

    if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
        render3D->SetShadowSceneBounds(bboxMin, bboxMax, anyVisible);
    }
    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetShadowSceneBounds(bboxMin, bboxMax, anyVisible);
    }
}

// 同步阴影贴图尺寸下拉框。
void MainWindow::SyncShadowTextureSizeCombo(uint32_t size) {
    if (!m_pComboTexSize) return;
    const QString text = QString::number(size);
    if (m_pComboTexSize->currentText() == text) return;
    const int index = m_pComboTexSize->findText(text);
    if (index < 0) return;
    const QSignalBlocker blocker(m_pComboTexSize);
    m_pComboTexSize->setCurrentIndex(index);
}

// 在状态栏显示阴影贴图消息。
void MainWindow::ShowShadowMapStatus(const std::string& message) {
    if (message.empty()) return;
    ShowMessage(QString::fromStdString(message), false);
}

// 弹出对话框打开模型文件。
void MainWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择模型文件"),
        "",
        QStringLiteral("模型文件 (*.obj *.gltf *.glb);;OBJ 文件 (*.obj);;glTF 文件 (*.gltf *.glb)"));

    if (filePath.isEmpty()) return;
    LoadSceneAssetFile(filePath);
}

// 按扩展名选择解析器并异步加载。
void MainWindow::LoadSceneAssetFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    if (!QFile::exists(filePath)) {
        ShowMessage(QStringLiteral("[错误] 无法打开文件: %1").arg(filePath), true);
        return;
    }

    const uint64_t loadGeneration = m_loadGeneration;
    auto onParsed = [this, filePath, loadGeneration](SceneAsset&& asset) {
        if (loadGeneration != m_loadGeneration) return;
        if (!m_renderer || m_renderer->IsShuttingDown() || m_renderer->IsDeviceLost()) return;
        ShowImportMessages(asset.messages);
        AddLoadedModel(filePath, std::move(asset));
    };

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    ObjParseRunnable::DiagnosticCallback diagnostics =
        [this, loadGeneration](const std::string& message, bool isError) {
            if (loadGeneration != m_loadGeneration) return;
            ShowMessage(QString::fromStdString(message), isError);
        };

    if (suffix == QStringLiteral("gltf") || suffix == QStringLiteral("glb")) {
        QThreadPool::globalInstance()->start(new GltfParseRunnable(
            filePath.toStdString(), onParsed, diagnostics));
    } else {
        QThreadPool::globalInstance()->start(new ObjParseRunnable(
            filePath.toStdString(), onParsed, diagnostics));
    }
}

// 打开最近文件菜单项对应路径。
void MainWindow::onRecentFileTriggered() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    LoadSceneAssetFile(action->data().toString());
}

// 把路径写入最近文件列表。
void MainWindow::AddRecentFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    while (m_recentFiles.size() > 5) {
        m_recentFiles.removeLast();
    }
    SaveRecentFiles();
    RebuildRecentMenu();
}

// 从设置读取最近文件。
void MainWindow::LoadRecentFiles() {
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    while (m_recentFiles.size() > 5) {
        m_recentFiles.removeLast();
    }
}

// 把最近文件写回设置。
void MainWindow::SaveRecentFiles() {
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
}

// 重建最近文件菜单。
void MainWindow::RebuildRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    if (m_recentFiles.isEmpty()) {
        QAction* emptyAction = m_recentMenu->addAction(QStringLiteral("无最近文件"));
        emptyAction->setEnabled(false);
        return;
    }
    for (const QString& path : m_recentFiles) {
        QAction* action = m_recentMenu->addAction(path);
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, &MainWindow::onRecentFileTriggered);
    }
}

// 把解析结果加入模型列表并建网格。
void MainWindow::AddLoadedModel(const QString& filePath, SceneAsset&& asset) {
    bool hasGeometry = false;
    for (const MeshData& mesh : asset.meshes) {
        if (!mesh.vertices.empty() && !mesh.indices.empty()) {
            hasGeometry = true;
            break;
        }
    }
    if (!hasGeometry) return;

    LoadedModel model;
    model.asset = std::move(asset);
    model.treeItem = new QTreeWidgetItem(m_modelsTreeItem);
    model.treeItem->setText(0, QFileInfo(filePath).fileName());
    m_modelsTreeItem->setExpanded(true);

    m_loadedModels.push_back(std::move(model));
    // 为新模型构建运行时节点层级（保留 glTF 的节点结构）。
    BuildModelHierarchy(m_loadedModels.back());
    RebuildSceneMeshes(true);
    RebuildProjectTree();
    // 任务 2.2：导入后在消息栏输出该模型的包围盒（归一化场景空间）。
    ShowModelBounds(m_loadedModels.back());
    AddRecentFile(filePath);
}

// 切换模型可见性。
void MainWindow::SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible) {
    auto it = std::find_if(m_loadedModels.begin(), m_loadedModels.end(),
        [treeItem](const LoadedModel& model) {
            return model.treeItem == treeItem;
        });
    if (it == m_loadedModels.end() || it->visible == visible) return;

    it->visible = visible;
    for (Object* mesh : it->meshes) {
        if (mesh) mesh->SetVisible(visible);
    }
    UpdateShadowSceneBounds();
}

// 从场景和列表移除模型。
void MainWindow::RemoveLoadedModel(QTreeWidgetItem* treeItem) {
    auto it = std::find_if(m_loadedModels.begin(), m_loadedModels.end(),
        [treeItem](const LoadedModel& model) {
            return model.treeItem == treeItem;
        });
    if (it == m_loadedModels.end()) return;

    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->WaitForIdle();
    }
    // 场景中该模型的根节点为 nodes[0].object；移除它会连带删除整棵子树。
    if (!it->nodes.empty() && it->nodes[0].object && m_scene) {
        m_scene->RemoveChild(it->nodes[0].object);
    }
    it->nodes.clear();
    it->meshes.clear();
    // 释放该模型的 GPU 纹理（延迟销毁）。
    ReleaseModelTextures(*it);

    delete it->treeItem;
    it->treeItem = nullptr;
    // 任务 2.3：若被移除的模型是当前选中模型（或索引在其之前），选择会失效/错位，清除之。
    const int removedIndex = static_cast<int>(it - m_loadedModels.begin());
    if (m_selectedModel >= removedIndex) {
        m_selectedModel = -1;
        m_selectedRuntimeNode = -1;
    }
    m_loadedModels.erase(it);

    // 任务 2.4：模型索引已变化，撤销/重做记录里的 (model, node) 会失效，清空并复位 Gizmo 状态。
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoCoalescing = false;
    EndGizmoDrag();
    m_gizmoHoverAxis = GizmoAxis::None;

    if (m_loadedModels.empty()) {
        // 已无剩余模型：复位到默认取景与坐标显示。
        ResetSceneView();
        UpdateShadowSceneBounds();
        RebuildProjectTree();
    } else {
        // 剩余模型重新归一化并重建（与“添加模型”对称）。
        RebuildSceneMeshes(true);
        // 模型索引已因 erase 前移，必须重建项目树刷新 UserRole 索引。
        RebuildProjectTree();
    }
    if (m_materialPanel && m_materialPanel->isVisible()) {
        RebuildMaterialPanel();
    }
}

// 清空全部已加载模型。
void MainWindow::ClearLoadedModels() {
    ++m_loadGeneration;

    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->WaitForIdle();
    }

    // 释放全部 GPU 纹理引用（延迟销毁）。
    ReleaseAllModelTextures();

    for (LoadedModel& model : m_loadedModels) {
        if (!model.nodes.empty() && model.nodes[0].object && m_scene) {
            m_scene->RemoveChild(model.nodes[0].object);
        }
        model.nodes.clear();
        model.meshes.clear();
        delete model.treeItem;
        model.treeItem = nullptr;
    }
    m_loadedModels.clear();
    m_selectedModel = -1;
    m_selectedRuntimeNode = -1;

    // 任务 2.4：清空场景后撤销/重做记录与 Gizmo 状态一并复位。
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoCoalescing = false;
    EndGizmoDrag();
    m_gizmoHoverAxis = GizmoAxis::None;

    ResetSceneView();
    UpdateShadowSceneBounds();
    RebuildProjectTree();
    if (m_materialPanel && m_materialPanel->isVisible()) {
        RebuildMaterialPanel();
    }
}

// 复位场景取景：相机、归一化与坐标显示（无模型或需要回到默认状态时使用）。
void MainWindow::ResetSceneView() {
    m_sceneViewDistance = 3.0f;
    m_sceneSourceCenter[0] = 0.0f;
    m_sceneSourceCenter[1] = 0.0f;
    m_sceneSourceCenter[2] = 0.0f;
    m_sceneNormalizationScale = 1.0f;
    // 场景根承载“归一化矩阵”，复位为单位变换。
    if (m_scene) {
        m_scene->SetLocalTransform(Transform{});
        m_scene->UpdateWorldTransforms(nullptr, false);
    }

    if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetCoordinateNormalization(Vec3{}, 1.0f);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetCoordinateNormalization(Vec3{}, 1.0f);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }

    m_coordX->setText(QStringLiteral("X: 0.000"));
    m_coordY->setText(QStringLiteral("Y: 0.000"));
    m_coordZ->setText(QStringLiteral("Z: 0.000"));
}

// 把相机对准场景或指定模型。
void MainWindow::FocusSceneOrModel(QTreeWidgetItem* treeItem) {
    if (m_loadedModels.empty()) return;

    const LoadedModel* selectedModel = nullptr;
    if (treeItem) {
        auto it = std::find_if(m_loadedModels.begin(), m_loadedModels.end(),
            [treeItem](const LoadedModel& model) {
                return model.treeItem == treeItem;
            });
        if (it == m_loadedModels.end()) return;
        selectedModel = &(*it);
    }

    // 统一的包围盒查询（归一化场景空间）：单模型或整个场景。
    const Aabb bounds = selectedModel ? ModelWorldBounds(*selectedModel) : SceneWorldBounds();
    if (AabbIsEmpty(bounds)) return;

    const Vec3 normalizedCenter = AabbCenter(bounds);
    const Vec3 boundsSize = AabbSize(bounds);
    const float sizeX = boundsSize.x;
    const float sizeY = boundsSize.y;
    const float sizeZ = boundsSize.z;
    QWindow* focusWindow = IsOpenGLBackend()
        ? static_cast<QWindow*>(m_openglWindow)
        : static_cast<QWindow*>(m_vulkanWindow);
    const int focusWidth = focusWindow ? focusWindow->width() : 1;
    const int focusHeight = focusWindow ? focusWindow->height() : 1;
    const float aspect = static_cast<float>(std::max(1, focusWidth)) /
                         static_cast<float>(std::max(1, focusHeight));
    constexpr float verticalHalfFov = 0.3926990817f; // 45度 / 2
    const float tanVertical = std::tan(verticalHalfFov);
    const float tanHorizontal = tanVertical * aspect;
    // 归一化场景空间 Z-up：显示深度 = sizeY，屏幕竖直 = sizeZ，水平 = sizeX。
    const float fitDistance = sizeY * 0.5f +
        std::max(sizeZ * 0.5f / tanVertical,
                 sizeX * 0.5f / tanHorizontal) + 0.1f;

    const float viewDistance = std::max(0.1f, fitDistance);
    if (!selectedModel) {
        m_sceneViewDistance = viewDistance;
    }

    if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(normalizedCenter);
        render3D->ResetView(viewDistance);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetOrbitCenter(normalizedCenter);
        render3D->ResetView(viewDistance);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
}

// 按渲染器类型安装 Mesh 工厂。
void MainWindow::ConfigureMeshFactory(Render* renderer) {
    if (!renderer) return;

    if (dynamic_cast<GLRender*>(renderer)) {
        renderer->SetMeshFactory([](Render* baseRenderer) -> Object* {
            auto* glRenderer = dynamic_cast<GLRender*>(baseRenderer);
            if (!glRenderer) return nullptr;

            auto* mesh = new GLMesh();
            mesh->SetRender(glRenderer);
            return mesh;
        });
        return;
    }

    if (dynamic_cast<VKRender*>(renderer)) {
        renderer->SetMeshFactory([](Render* baseRenderer) -> Object* {
            auto* vkRenderer = dynamic_cast<VKRender*>(baseRenderer);
            if (!vkRenderer) return nullptr;

            auto* mesh = new VKMesh();
            mesh->SetRender(vkRenderer);
            return mesh;
        });
    }
}

// 按当前后端重建场景网格（默认重算场景归一化）。
void MainWindow::RebuildSceneMeshes() {
    RebuildSceneMeshes(true);
}

// 按当前后端重建场景网格；recomputeNormalization 为 true 时重算场景归一化。
// 运行时节点层级（model.nodes）在后端切换后保留其逻辑结构，这里只重建场景对象。
void MainWindow::RebuildSceneMeshes(bool recomputeNormalization) {
    const bool useOpenGL = IsOpenGLBackend()
        && dynamic_cast<GLRender3D*>(m_openglRenderer) != nullptr;
    if (useOpenGL) {
        if (!m_openglRenderer || !m_openglRenderer->IsInitialized() || m_loadedModels.empty()) return;
    } else if (IsOpenGLBackend()) {
        return;
    } else {
        if (!m_renderer || !m_renderer->IsInitialized() || m_loadedModels.empty()) return;
    }

    // 归一化只与“模型空间”的当前变换有关，与后端无关。
    if (recomputeNormalization) {
        ComputeSceneNormalization();
    }
    // 场景根承载“归一化矩阵”（原始模型坐标 -> 归一化场景空间）。
    m_scene->SetLocalTransform(TransformFromMatrix(SceneNormalizationMatrix()));
    // 世界坐标回读需要“归一化场景空间 -> 原始坐标”的反变换（与场景根一致）。
    const Vec3 normCenter = { m_sceneSourceCenter[0], m_sceneSourceCenter[1],
                              m_sceneSourceCenter[2] };
    if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
        render3D->SetCoordinateNormalization(normCenter, m_sceneNormalizationScale);
    }
    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetCoordinateNormalization(normCenter, m_sceneNormalizationScale);
    }

    // 删除旧场景对象前等待 GPU 空闲（避免销毁仍在使用的缓冲/VAO）。
    if (useOpenGL) {
        if (m_openglRenderer) m_openglRenderer->WaitForIdle();
    } else if (m_renderer) {
        m_renderer->WaitForIdle();
    }
    m_scene->Clear();
    for (LoadedModel& model : m_loadedModels) {
        model.meshes.clear();
        for (RuntimeNode& node : model.nodes) {
            node.object = nullptr;
            for (RuntimeMesh& mesh : node.meshes) {
                mesh.object = nullptr;
            }
        }
    }

    for (LoadedModel& model : m_loadedModels) {
        auto isDeleted = [&model](size_t index) {
            return index < model.nodeDeleted.size() && model.nodeDeleted[index] != 0;
        };
        // 1. 为每个“未删除”的运行时节点创建一个 Layer 场景对象（承载变换）。
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (isDeleted(i)) continue;
            auto* layer = new Layer();
            layer->SetLocalTransform(model.nodes[i].local);
            layer->SetVisible(true);
            model.nodes[i].object = layer;
        }
        // 2. 为每个网格条目创建网格对象，挂到所属节点。
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (isDeleted(i)) continue;
            RuntimeNode& node = model.nodes[i];
            for (RuntimeMesh& mesh : node.meshes) {
                if (mesh.assetMeshIndex < 0 ||
                    mesh.assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
                    continue;
                }
                Object* meshObject = CreateMeshObject(model, mesh.assetMeshIndex);
                mesh.object = meshObject;
                if (meshObject && node.object) {
                    node.object->AddChild(meshObject);
                    model.meshes.push_back(meshObject);
                }
            }
        }
        // 3. 连接节点父子关系，并把模型根挂到场景根。
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (isDeleted(i)) continue;
            RuntimeNode& node = model.nodes[i];
            if (!node.object) continue;
            for (int child : node.children) {
                if (child < 0 || child >= static_cast<int>(model.nodes.size())) continue;
                if (isDeleted(static_cast<size_t>(child))) continue;
                if (model.nodes[child].object) {
                    node.object->AddChild(model.nodes[child].object);
                }
            }
        }
        if (!model.nodes.empty() && model.nodes[0].object) {
            m_scene->AddChild(model.nodes[0].object);
        }
    }

    // 场景图刷新一次世界矩阵。
    m_scene->UpdateWorldTransforms(nullptr, false);

    // 场景取景：仅在新模型加入 / 移除 / 后端切换（重算归一化）时复位相机，
    // 变换编辑（删除/复制节点）不重置相机，避免“改一个节点把视角弹回”。
    if (recomputeNormalization) {
        // 统一的场景包围盒查询（归一化场景空间）。
        const Aabb sceneBounds = SceneWorldBounds();
        const bool any = !AabbIsEmpty(sceneBounds);
        const Vec3 center = any ? AabbCenter(sceneBounds) : Vec3{ 0.0f, 0.0f, 0.0f };
        const Vec3 sceneSize = AabbSize(sceneBounds);
        const float sizeX = sceneSize.x;
        const float sizeY = sceneSize.y;
        const float sizeZ = sceneSize.z;

        QWindow* viewWindow = useOpenGL
            ? static_cast<QWindow*>(m_openglWindow)
            : static_cast<QWindow*>(m_vulkanWindow);
        const int viewWidth = viewWindow ? viewWindow->width() : 1;
        const int viewHeight = viewWindow ? viewWindow->height() : 1;
        const float aspect = static_cast<float>(std::max(1, viewWidth)) /
                             static_cast<float>(std::max(1, viewHeight));
        constexpr float verticalHalfFov = 0.3926990817f; // 45° / 2
        const float tanVertical = std::tan(verticalHalfFov);
        const float tanHorizontal = tanVertical * aspect;
        // 归一化场景空间为 Z-up，显示基础朝向把场景 Z 映射到屏幕上方、场景 Y 映射到
        // 深度方向。故取景距离：深度 = sizeY，屏幕竖直 = sizeZ，水平 = sizeX。
        const float fitDistance = sizeY * 0.5f +
            std::max(sizeZ * 0.5f / tanVertical, sizeX * 0.5f / tanHorizontal) + 0.1f;
        m_sceneViewDistance = std::max(3.0f, fitDistance);

        const bool orthographicEnabled =
            m_orthographicCheck && m_orthographicCheck->isChecked();
        if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
            render3D->SetOrbitCenter(center);
            render3D->ResetView(m_sceneViewDistance);
            render3D->SetOrthographicEnabled(orthographicEnabled);
        }
        if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
            render3D->SetOrbitCenter(center);
            render3D->ResetView(m_sceneViewDistance);
            render3D->SetOrthographicEnabled(orthographicEnabled);
        }
    }

    // 网格就绪后导入纹理（CPU 缓存 + 当前后端 GPU 上传）。
    ImportModelTextures();
    // 纹理句柄就绪后，构建逐 SubMesh 绘制信息（材质 + 纹理 + 可见性）。
    RebuildSubMeshDrawInfos();
    // 任务 2.3：场景对象重建后重新应用选中高亮（高亮标志随对象重建而丢失）。
    ApplySelectionHighlight();
    UpdateShadowSceneBounds();
}

// 为一个网格资产创建网格对象（按当前后端）并上传其“节点局部空间”顶点。
Object* MainWindow::CreateMeshObject(LoadedModel& model, int assetMeshIndex) {
    if (assetMeshIndex < 0 || assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
        return nullptr;
    }
    const bool useOpenGL = IsOpenGLBackend();
    Render* active = useOpenGL
        ? static_cast<Render*>(m_openglRenderer)
        : static_cast<Render*>(m_renderer);
    if (!active) return nullptr;

    Object* object = active->CreateMesh();
    if (!object) return nullptr;
    object->SetVisible(model.visible);

    // 任务 2.1：顶点按“节点局部空间”上传，不再做 CPU 归一化；变换由场景图施加。
    const MeshData& mesh = model.asset.meshes[static_cast<size_t>(assetMeshIndex)];
    // 任务 2.2：把网格局部包围盒写入对象，供统一的包围盒查询（GetWorldBounds）使用。
    object->SetLocalBounds(mesh.bounds);
    if (auto* vkMesh = dynamic_cast<VKMesh*>(object)) {
        vkMesh->SetMeshData(mesh);
    } else if (auto* glMesh = dynamic_cast<GLMesh*>(object)) {
        glMesh->SetMeshDataSync(mesh);
    }
    return object;
}

// 为所有已加载模型的网格构建逐 SubMesh 绘制信息。
void MainWindow::RebuildSubMeshDrawInfos() {
    for (LoadedModel& model : m_loadedModels) {
        for (RuntimeNode& node : model.nodes) {
            for (RuntimeMesh& mesh : node.meshes) {
                if (!mesh.object) continue;
                if (mesh.assetMeshIndex < 0 ||
                    mesh.assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
                    continue;
                }
                const MeshData& source =
                    model.asset.meshes[static_cast<size_t>(mesh.assetMeshIndex)];
                std::vector<SubMeshDrawInfo> infos = BuildSubMeshDrawInfos(
                    source, model.asset.materials, model.textureHandles);
                // 应用已保存的材质可见性。
                for (SubMeshDrawInfo& info : infos) {
                    const auto found = model.materialVisible.find(info.materialIndex);
                    if (found != model.materialVisible.end()) {
                        info.visible = found->second;
                    }
                }
                if (auto* vkMesh = dynamic_cast<VKMesh*>(mesh.object)) {
                    vkMesh->SetSubMeshDrawInfos(std::move(infos));
                } else if (auto* glMesh = dynamic_cast<GLMesh*>(mesh.object)) {
                    glMesh->SetSubMeshDrawInfos(std::move(infos));
                }
            }
        }
    }

    if (m_materialPanel && m_materialPanel->isVisible()) {
        RebuildMaterialPanel();
    }
}

// ---------------- 任务 2.1：运行时节点层级 / 变换编辑 ----------------

// 为一个模型构建运行时节点层级（模型根 + 各资产节点 + 网格挂载）。
void MainWindow::BuildModelHierarchy(LoadedModel& model) {
    model.nodes.clear();
    model.nodeDeleted.clear();

    const bool hasNodes = !model.asset.nodes.empty();

    // 模型根节点：统一做 Y-up -> Z-up 旋转（+90° 绕 X），把资产数据的上轴对齐到
    // Z-up 世界。glTF 规范为 Y-up；OBJ 无上轴元数据，这里按通用 Y-up 约定处理。
    // 渲染器显示变换含一个 -90° 绕 X 的基础朝向，两者相消，故初始画面与旧版一致。
    RuntimeNode root;
    root.name = model.treeItem ? model.treeItem->text(0).toStdString() : std::string("模型");
    root.parent = -1;
    root.sourceAssetNode = -1;
    root.local.rotationDegrees = { 90.0f, 0.0f, 0.0f };
    root.defaultLocal = root.local;
    model.nodes.push_back(root);
    model.nodeDeleted.push_back(0);

    if (hasNodes) {
        // 资产节点 -> 运行时节点索引。
        std::vector<int> assetToRuntime(model.asset.nodes.size(), -1);
        for (size_t i = 0; i < model.asset.nodes.size(); ++i) {
            RuntimeNode node;
            node.name = model.asset.nodes[i].name;
            node.sourceAssetNode = static_cast<int>(i);
            node.parent = -1;
            node.local = TransformFromMatrix(model.asset.nodes[i].localTransform);
            node.defaultLocal = node.local;
            assetToRuntime[i] = static_cast<int>(model.nodes.size());
            model.nodes.push_back(node);
            model.nodeDeleted.push_back(0);
        }
        // 连接父子（根节点默认挂到模型根）。
        for (size_t i = 0; i < model.asset.nodes.size(); ++i) {
            const int runtime = assetToRuntime[i];
            const int parentAsset = model.asset.nodes[i].parent;
            int parentRuntime = 0;
            if (parentAsset >= 0 && parentAsset < static_cast<int>(assetToRuntime.size()) &&
                assetToRuntime[static_cast<size_t>(parentAsset)] >= 0) {
                parentRuntime = assetToRuntime[static_cast<size_t>(parentAsset)];
            }
            model.nodes[runtime].parent = parentRuntime;
            model.nodes[parentRuntime].children.push_back(runtime);
        }
    }

    // 挂载网格：glTF 按 MeshData::nodeIndex 挂到对应运行时节点；OBJ 挂到模型根。
    for (size_t mi = 0; mi < model.asset.meshes.size(); ++mi) {
        const int nodeIndex = model.asset.meshes[mi].nodeIndex;
        int target = 0;
        if (nodeIndex >= 0 && hasNodes) {
            for (size_t r = 0; r < model.nodes.size(); ++r) {
                if (model.nodes[r].sourceAssetNode == nodeIndex) {
                    target = static_cast<int>(r);
                    break;
                }
            }
        }
        RuntimeMesh mesh;
        mesh.assetMeshIndex = static_cast<int>(mi);
        mesh.object = nullptr;
        model.nodes[target].meshes.push_back(mesh);
    }
}

// 计算某运行时节点在“模型空间”（含模型根变换，不含场景归一化）的世界矩阵。
Mat4 MainWindow::ModelSpaceNodeMatrix(const LoadedModel& model, int runtimeNode) const {
    std::vector<int> chain;
    int cur = runtimeNode;
    while (cur >= 0 && cur < static_cast<int>(model.nodes.size())) {
        chain.push_back(cur);
        cur = model.nodes[cur].parent;
    }
    Mat4 result = TransformIdentityMatrix();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        result = TransformMultiply(result, model.nodes[*it].local.ToMatrix());
    }
    return result;
}

// 场景归一化矩阵 M_norm（原始模型坐标 -> 归一化场景空间）= S * T(-center)。
Mat4 MainWindow::SceneNormalizationMatrix() const {
    const float s = m_sceneNormalizationScale;
    const Mat4 scale = TransformMatrixFromTrs(
        Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ s, s, s });
    const Mat4 translate = TransformMatrixFromTrs(
        Vec3{ -m_sceneSourceCenter[0], -m_sceneSourceCenter[1], -m_sceneSourceCenter[2] },
        Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 1.0f, 1.0f, 1.0f });
    return TransformMultiply(scale, translate);
}

// 累计某模型所有网格在“归一化场景空间”的包围盒（含场景归一化）。
void MainWindow::AccumulateModelBounds(const LoadedModel& model, Vec3& boundsMin,
                                       Vec3& boundsMax, bool& any) const {
    const Mat4 norm = SceneNormalizationMatrix();
    for (size_t r = 0; r < model.nodes.size(); ++r) {
        if (r < model.nodeDeleted.size() && model.nodeDeleted[r]) continue;
        const Mat4 world = TransformMultiply(norm, ModelSpaceNodeMatrix(model, static_cast<int>(r)));
        for (const RuntimeMesh& mesh : model.nodes[r].meshes) {
            if (mesh.assetMeshIndex < 0 ||
                mesh.assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
                continue;
            }
            const Aabb& box = model.asset.meshes[static_cast<size_t>(mesh.assetMeshIndex)].bounds;
            // 用统一的 AabbTransform（8 角点变换重求并）替代手写角点循环。
            const Aabb worldBox = AabbTransform(box, world);
            if (AabbIsEmpty(worldBox)) continue;
            boundsMin.x = std::min(boundsMin.x, worldBox.min.x);
            boundsMin.y = std::min(boundsMin.y, worldBox.min.y);
            boundsMin.z = std::min(boundsMin.z, worldBox.min.z);
            boundsMax.x = std::max(boundsMax.x, worldBox.max.x);
            boundsMax.y = std::max(boundsMax.y, worldBox.max.y);
            boundsMax.z = std::max(boundsMax.z, worldBox.max.z);
            any = true;
        }
    }
}

// 计算某模型在“归一化场景空间”的整体世界包围盒（任务 2.2）；无几何返回空盒。
Aabb MainWindow::ModelWorldBounds(const LoadedModel& model) const {
    Vec3 boundsMin = { std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max() };
    Vec3 boundsMax = { std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest() };
    bool any = false;
    AccumulateModelBounds(model, boundsMin, boundsMax, any);
    if (!any) return AabbEmpty();
    Aabb out;
    out.min = boundsMin;
    out.max = boundsMax;
    return out;
}

// 计算整个场景在“归一化场景空间”的世界包围盒（所有模型并集，任务 2.2）。
Aabb MainWindow::SceneWorldBounds() const {
    Aabb result = AabbEmpty();
    for (const LoadedModel& model : m_loadedModels) {
        result = AabbUnion(result, ModelWorldBounds(model));
    }
    return result;
}

// 计算场景归一化：更新 m_sceneSourceCenter / m_sceneNormalizationScale。
void MainWindow::ComputeSceneNormalization() {
    Vec3 boundsMin = { std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max() };
    Vec3 boundsMax = { std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest() };
    bool any = false;
    // 在“模型空间”累计（不含归一化，否则会自引用）。
    for (const LoadedModel& model : m_loadedModels) {
        for (size_t r = 0; r < model.nodes.size(); ++r) {
            if (r < model.nodeDeleted.size() && model.nodeDeleted[r]) continue;
            const Mat4 world = ModelSpaceNodeMatrix(model, static_cast<int>(r));
            for (const RuntimeMesh& mesh : model.nodes[r].meshes) {
                if (mesh.assetMeshIndex < 0 ||
                    mesh.assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
                    continue;
                }
                const Aabb& box =
                    model.asset.meshes[static_cast<size_t>(mesh.assetMeshIndex)].bounds;
                // 统一的 AabbTransform（8 角点变换重求并）替代手写角点循环。
                const Aabb worldBox = AabbTransform(box, world);
                if (AabbIsEmpty(worldBox)) continue;
                boundsMin.x = std::min(boundsMin.x, worldBox.min.x);
                boundsMin.y = std::min(boundsMin.y, worldBox.min.y);
                boundsMin.z = std::min(boundsMin.z, worldBox.min.z);
                boundsMax.x = std::max(boundsMax.x, worldBox.max.x);
                boundsMax.y = std::max(boundsMax.y, worldBox.max.y);
                boundsMax.z = std::max(boundsMax.z, worldBox.max.z);
                any = true;
            }
        }
    }

    if (!any) {
        m_sceneSourceCenter[0] = 0.0f;
        m_sceneSourceCenter[1] = 0.0f;
        m_sceneSourceCenter[2] = 0.0f;
        m_sceneNormalizationScale = 1.0f;
        return;
    }
    const Vec3 center = {
        (boundsMin.x + boundsMax.x) * 0.5f,
        (boundsMin.y + boundsMax.y) * 0.5f,
        (boundsMin.z + boundsMax.z) * 0.5f
    };
    const float maxSize = std::max({ boundsMax.x - boundsMin.x,
                                     boundsMax.y - boundsMin.y,
                                     boundsMax.z - boundsMin.z });
    const float scale = maxSize > std::numeric_limits<float>::epsilon()
        ? 2.0f / maxSize : 1.0f;
    m_sceneSourceCenter[0] = center.x;
    m_sceneSourceCenter[1] = center.y;
    m_sceneSourceCenter[2] = center.z;
    m_sceneNormalizationScale = scale;
}

// 变换变更后：刷新阴影范围（世界矩阵在渲染循环中按脏标记刷新）。
void MainWindow::RefreshAfterTransformChange() {
    UpdateShadowSceneBounds();
    m_lastShadowPassTimer.invalidate();
}

// 重建项目树的节点层级。
void MainWindow::RebuildProjectTree() {
    if (!m_projectPanel || !m_modelsTreeItem) return;
    const QSignalBlocker blocker(m_projectPanel);
    // 清除旧节点项（保留模型项本身）。
    for (size_t i = 0; i < m_loadedModels.size(); ++i) {
        LoadedModel& model = m_loadedModels[i];
        if (!model.treeItem) {
            model.treeItem = new QTreeWidgetItem(m_modelsTreeItem);
            model.treeItem->setText(0,
                QFileInfo(QString::fromStdString(model.asset.sourcePath)).fileName());
        } else if (model.treeItem->parent() != m_modelsTreeItem) {
            model.treeItem->parent()->removeChild(model.treeItem);
            m_modelsTreeItem->addChild(model.treeItem);
        }
        model.treeItem->setData(0, Qt::UserRole, static_cast<int>(i));
        // 模型项自身映射到模型根节点（nodes[0]）：OBJ 等无子节点的模型也可直接编辑其根变换。
        model.treeItem->setData(0, Qt::UserRole + 1, 0);
        // 清掉旧节点项（takeChildren 转移所有权，需手动释放）。
        const QList<QTreeWidgetItem*> oldChildren = model.treeItem->takeChildren();
        for (QTreeWidgetItem* child : oldChildren) {
            delete child;
        }
        PopulateNodeTreeItems(i, 0, model.treeItem);
        model.treeItem->setExpanded(true);
    }
    m_modelsTreeItem->setExpanded(true);
}

// 在消息栏输出某模型的包围盒（归一化场景空间，任务 2.2）。
void MainWindow::ShowModelBounds(const LoadedModel& model) {
    const Aabb box = ModelWorldBounds(model);
    const QString name = model.treeItem
        ? model.treeItem->text(0)
        : QFileInfo(QString::fromStdString(model.asset.sourcePath)).fileName();
    if (AabbIsEmpty(box)) {
        ShowMessage(QStringLiteral("包围盒 [%1]：（无几何）").arg(name), false);
        return;
    }
    ShowMessage(QStringLiteral("包围盒 [%1]：min(%2, %3, %4)  max(%5, %6, %7)")
        .arg(name)
        .arg(box.min.x, 0, 'f', 3)
        .arg(box.min.y, 0, 'f', 3)
        .arg(box.min.z, 0, 'f', 3)
        .arg(box.max.x, 0, 'f', 3)
        .arg(box.max.y, 0, 'f', 3)
        .arg(box.max.z, 0, 'f', 3), false);
}

// 把某模型节点层级写入项目树。
void MainWindow::PopulateNodeTreeItems(size_t modelIndex, int runtimeNode,
                                       QTreeWidgetItem* parentItem) {
    LoadedModel& model = m_loadedModels[modelIndex];
    if (runtimeNode < 0 || runtimeNode >= static_cast<int>(model.nodes.size())) return;
    if (runtimeNode < static_cast<int>(model.nodeDeleted.size()) && model.nodeDeleted[runtimeNode]) {
        return;
    }
    const RuntimeNode& node = model.nodes[runtimeNode];
    // 模型根（0）不再单列一项，其子节点直接挂在模型项下。
    QTreeWidgetItem* item = parentItem;
    if (runtimeNode != 0) {
        item = new QTreeWidgetItem(parentItem);
        QString label = node.name.empty()
            ? QStringLiteral("节点 %1").arg(runtimeNode)
            : QString::fromStdString(node.name);
        if (node.isClone) label += QStringLiteral("（副本）");
        item->setText(0, label);
        item->setData(0, Qt::UserRole, static_cast<int>(modelIndex));
        item->setData(0, Qt::UserRole + 1, runtimeNode);
        item->setExpanded(true);
    }
    for (int child : node.children) {
        PopulateNodeTreeItems(modelIndex, child, item);
    }
}

// 找到项目树项对应的模型索引；无效返回 -1。
int MainWindow::FindModelIndexByTreeItem(QTreeWidgetItem* item) const {
    if (!item) return -1;
    // 节点项直接带有模型索引。
    const QVariant modelData = item->data(0, Qt::UserRole);
    const QVariant nodeData = item->data(0, Qt::UserRole + 1);
    if (modelData.isValid() && nodeData.isValid() && nodeData.toInt() >= 0) {
        return modelData.toInt();
    }
    // 模型项：与 model.treeItem 比对。
    for (size_t i = 0; i < m_loadedModels.size(); ++i) {
        if (m_loadedModels[i].treeItem == item) return static_cast<int>(i);
    }
    return -1;
}

// 解析项目树项对应的 (模型索引, 运行时节点索引)；无效返回 false。
bool MainWindow::ResolveTreeItem(QTreeWidgetItem* item, size_t& modelIndex,
                                 int& runtimeNode) const {
    if (!item) return false;
    const QVariant modelData = item->data(0, Qt::UserRole);
    const QVariant nodeData = item->data(0, Qt::UserRole + 1);
    if (!modelData.isValid() || !nodeData.isValid()) return false;
    const int mi = modelData.toInt();
    const int rn = nodeData.toInt();
    if (mi < 0 || mi >= static_cast<int>(m_loadedModels.size())) return false;
    if (rn < 0 || rn >= static_cast<int>(m_loadedModels[static_cast<size_t>(mi)].nodes.size())) {
        return false;
    }
    modelIndex = static_cast<size_t>(mi);
    runtimeNode = rn;
    return true;
}

// 项目树选择变化：刷新变换面板 + 同步视口高亮 + 输出选中提示（任务 2.3）。
void MainWindow::OnProjectSelectionChanged() {
    if (!m_projectPanel) return;
    QTreeWidgetItem* item = m_projectPanel->currentItem();
    // 仅当该项确实处于选中状态时才算有效选择（clearSelection 后 currentItem 可能残留）。
    if (item && !item->isSelected()) item = nullptr;
    size_t modelIndex = 0;
    int runtimeNode = -1;
    const bool hadSelection = m_selectedModel >= 0 && m_selectedRuntimeNode >= 0;
    if (item && ResolveTreeItem(item, modelIndex, runtimeNode)) {
        m_selectedModel = static_cast<int>(modelIndex);
        m_selectedRuntimeNode = runtimeNode;
    } else {
        m_selectedModel = -1;
        m_selectedRuntimeNode = -1;
    }
    SyncTransformPanelFromSelection();
    // 任务 2.3：项目树选中 -> 视口高亮同步。
    ApplySelectionHighlight();
    // 任务 2.4：切换选择对象时清除 Gizmo 拖拽/悬停状态，避免残留错误状态。
    EndGizmoDrag();
    m_gizmoHoverAxis = GizmoAxis::None;
    m_undoCoalescing = false;
    // 任务 2.3：在消息栏输出选中/取消提示（树选中无 SubMesh/三角形/坐标信息）。
    if (m_selectedModel >= 0 && m_selectedRuntimeNode >= 0) {
        ShowMessage(QStringLiteral("选中 [%1]")
            .arg(NodeDisplayName(m_selectedModel, m_selectedRuntimeNode)), false);
    } else if (hadSelection) {
        ShowMessage(QStringLiteral("已取消选择"), false);
    }
}

// 点击项目树空白处：清除选择并（确有选中时）提示“已取消选择”。
void MainWindow::ClearSelectionByEmptyClick() {
    const bool hadSelection = m_selectedModel >= 0 && m_selectedRuntimeNode >= 0;
    // 清掉项目树的选中与 current（信号屏蔽：状态与提示由本函数统一处理）。
    if (m_projectPanel) {
        const QSignalBlocker blocker(m_projectPanel);
        m_projectPanel->clearSelection();
        m_projectPanel->setCurrentItem(nullptr);
    }
    m_selectedModel = -1;
    m_selectedRuntimeNode = -1;
    ApplySelectionHighlight();
    SyncTransformPanelFromSelection();
    // 任务 2.4：取消选择时清除 Gizmo 拖拽/悬停状态。
    EndGizmoDrag();
    m_gizmoHoverAxis = GizmoAxis::None;
    if (hadSelection) {
        ShowMessage(QStringLiteral("已取消选择"), false);
    }
}

// 用选中节点的变换刷新变换面板。
void MainWindow::SyncTransformPanelFromSelection() {
    m_syncingTransformPanel = true;
    auto setSpins = [this](QDoubleSpinBox* sx, QDoubleSpinBox* sy, QDoubleSpinBox* sz,
                           const Vec3& v) {
        if (sx) sx->setValue(v.x);
        if (sy) sy->setValue(v.y);
        if (sz) sz->setValue(v.z);
    };
    const bool valid = m_selectedModel >= 0 &&
        m_selectedModel < static_cast<int>(m_loadedModels.size()) &&
        m_selectedRuntimeNode >= 0 &&
        m_selectedRuntimeNode <
            static_cast<int>(m_loadedModels[static_cast<size_t>(m_selectedModel)].nodes.size());
    if (valid) {
        const LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
        const RuntimeNode& node = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)];
        setSpins(m_spinTransX, m_spinTransY, m_spinTransZ, node.local.translation);
        setSpins(m_spinRotX, m_spinRotY, m_spinRotZ, node.local.rotationDegrees);
        setSpins(m_spinScaleX, m_spinScaleY, m_spinScaleZ, node.local.scale);
        if (m_transformSelectionLabel) {
            QString label = node.name.empty()
                ? QStringLiteral("节点 %1").arg(m_selectedRuntimeNode)
                : QString::fromStdString(node.name);
            if (m_selectedRuntimeNode == 0) label = QStringLiteral("模型根（%1）").arg(label);
            m_transformSelectionLabel->setText(QStringLiteral("选中：%1").arg(label));
        }
    } else {
        setSpins(m_spinTransX, m_spinTransY, m_spinTransZ, Vec3{ 0, 0, 0 });
        setSpins(m_spinRotX, m_spinRotY, m_spinRotZ, Vec3{ 0, 0, 0 });
        setSpins(m_spinScaleX, m_spinScaleY, m_spinScaleZ, Vec3{ 1, 1, 1 });
        if (m_transformSelectionLabel) {
            m_transformSelectionLabel->setText(QStringLiteral("未选中节点"));
        }
    }
    m_syncingTransformPanel = false;
}

// 把变换面板数值写入当前选中节点。
void MainWindow::ApplyTransformPanelToSelection() {
    if (m_syncingTransformPanel) return;
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode < 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) return;

    RuntimeNode& node = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)];
    // 任务 2.4：数值编辑也纳入撤销（连续编辑同一节点合并为一条记录）。
    TransformSnapshot snapshot;
    snapshot.model = m_selectedModel;
    snapshot.node = m_selectedRuntimeNode;
    snapshot.local = node.local;
    PushUndoSnapshot(snapshot, true);

    node.local.translation = Vec3{
        static_cast<float>(m_spinTransX ? m_spinTransX->value() : 0.0),
        static_cast<float>(m_spinTransY ? m_spinTransY->value() : 0.0),
        static_cast<float>(m_spinTransZ ? m_spinTransZ->value() : 0.0) };
    node.local.rotationDegrees = Vec3{
        static_cast<float>(m_spinRotX ? m_spinRotX->value() : 0.0),
        static_cast<float>(m_spinRotY ? m_spinRotY->value() : 0.0),
        static_cast<float>(m_spinRotZ ? m_spinRotZ->value() : 0.0) };
    node.local.scale = Vec3{
        static_cast<float>(m_spinScaleX ? m_spinScaleX->value() : 1.0),
        static_cast<float>(m_spinScaleY ? m_spinScaleY->value() : 1.0),
        static_cast<float>(m_spinScaleZ ? m_spinScaleZ->value() : 1.0) };
    if (node.object) {
        node.object->SetLocalTransform(node.local);
    }
    RefreshAfterTransformChange();
}

// 选中节点：重置变换。
void MainWindow::ResetSelectedNodeTransform() {
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode < 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) return;
    RuntimeNode& node = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)];
    // 任务 2.4：重置也纳入撤销。
    TransformSnapshot snapshot;
    snapshot.model = m_selectedModel;
    snapshot.node = m_selectedRuntimeNode;
    snapshot.local = node.local;
    PushUndoSnapshot(snapshot, false);
    node.local = node.defaultLocal;
    if (node.object) node.object->SetLocalTransform(node.local);
    SyncTransformPanelFromSelection();
    RefreshAfterTransformChange();
}

// 标记某运行时节点及其整棵子树为“已删除”。
void MainWindow::MarkSubtreeDeleted(LoadedModel& model, int runtimeNode) {
    if (runtimeNode < 0 || runtimeNode >= static_cast<int>(model.nodes.size())) return;
    if (runtimeNode == 0) return;  // 模型根不可删除
    model.nodeDeleted[static_cast<size_t>(runtimeNode)] = 1;
    for (int child : model.nodes[static_cast<size_t>(runtimeNode)].children) {
        MarkSubtreeDeleted(model, child);
    }
}

// ---------------- 任务 2.4：Transform Gizmo 与编辑闭环 ----------------

// 把归一化场景空间向量换算到某运行时节点的父空间（Gizmo 轴 -> 局部平移用）。
// 父空间 = 该节点 local.translation 所在的空间（父节点的“模型空间矩阵”）。
Vec3 MainWindow::SceneVectorToParentSpace(const LoadedModel& model, int runtimeNode,
                                          const Vec3& v) const {
    const int parent = (runtimeNode >= 0 && runtimeNode < static_cast<int>(model.nodes.size()))
        ? model.nodes[static_cast<size_t>(runtimeNode)].parent : -1;
    const Mat4 parentModel = (parent >= 0)
        ? ModelSpaceNodeMatrix(model, parent) : TransformIdentityMatrix();
    const Mat4 parentNss = TransformMultiply(SceneNormalizationMatrix(), parentModel);
    Mat4 inv{};
    if (!TransformInvert(parentNss, inv)) return v;
    return TransformVector(inv, v);
}

// 把归一化场景空间点换算到某运行时节点的父空间（Gizmo 枢轴 -> 局部平移用）。
Vec3 MainWindow::ScenePointToParentSpace(const LoadedModel& model, int runtimeNode,
                                         const Vec3& p) const {
    const int parent = (runtimeNode >= 0 && runtimeNode < static_cast<int>(model.nodes.size()))
        ? model.nodes[static_cast<size_t>(runtimeNode)].parent : -1;
    const Mat4 parentModel = (parent >= 0)
        ? ModelSpaceNodeMatrix(model, parent) : TransformIdentityMatrix();
    const Mat4 parentNss = TransformMultiply(SceneNormalizationMatrix(), parentModel);
    Mat4 inv{};
    if (!TransformInvert(parentNss, inv)) return p;
    return TransformPoint(inv, p);
}

// 某运行时节点（含整棵子树）在“归一化场景空间”的包围盒；无几何返回空盒。
Aabb MainWindow::NodeSubtreeWorldBounds(const LoadedModel& model, int runtimeNode) const {
    Aabb result = AabbEmpty();
    if (runtimeNode < 0 || runtimeNode >= static_cast<int>(model.nodes.size())) return result;
    const Mat4 norm = SceneNormalizationMatrix();
    // 以该节点为根的子树（跳过已删除节点）。
    std::vector<int> stack{ runtimeNode };
    while (!stack.empty()) {
        const int r = stack.back();
        stack.pop_back();
        if (r < 0 || r >= static_cast<int>(model.nodes.size())) continue;
        if (r < static_cast<int>(model.nodeDeleted.size()) && model.nodeDeleted[r]) continue;
        const Mat4 world = TransformMultiply(norm, ModelSpaceNodeMatrix(model, r));
        for (const RuntimeMesh& mesh : model.nodes[static_cast<size_t>(r)].meshes) {
            if (mesh.assetMeshIndex < 0 ||
                mesh.assetMeshIndex >= static_cast<int>(model.asset.meshes.size())) {
                continue;
            }
            const Aabb& box = model.asset.meshes[static_cast<size_t>(mesh.assetMeshIndex)].bounds;
            result = AabbUnion(result, AabbTransform(box, world));
        }
        for (int child : model.nodes[static_cast<size_t>(r)].children) {
            stack.push_back(child);
        }
    }
    return result;
}

// 当前活动三维渲染器（Vulkan 或 OpenGL）。
static Render* ActiveRender3D(Render* vulkan, Render* opengl, bool useOpenGL) {
    return useOpenGL ? opengl : vulkan;
}

namespace {
// Gizmo 枢轴补偿用的轻量向量工具（本文件内）。
Vec3 GizmoVecSub(const Vec3& a, const Vec3& b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
}  // namespace

// 把 Gizmo 模式 / 坐标空间 / 吸附的选中态同步到 UI。
void MainWindow::SyncGizmoUi() {
    if (m_gizmoModeCombo) {
        const QSignalBlocker blocker(m_gizmoModeCombo);
        int index = 0;
        if (m_gizmoMode == GizmoMode::Rotate) index = 1;
        else if (m_gizmoMode == GizmoMode::Scale) index = 2;
        m_gizmoModeCombo->setCurrentIndex(index);
    }
    if (m_gizmoWorldSpaceCheck) {
        const QSignalBlocker blocker(m_gizmoWorldSpaceCheck);
        m_gizmoWorldSpaceCheck->setChecked(m_gizmoWorldSpace);
    }
    if (m_gizmoSnapCheck) {
        const QSignalBlocker blocker(m_gizmoSnapCheck);
        m_gizmoSnapCheck->setChecked(m_gizmoSnapEnabled);
    }
}

// 构建当前选中节点的 Gizmo 放置帧（归一化场景空间，固定屏幕尺寸）；无选中返回 false。
bool MainWindow::BuildGizmoFrame(GizmoFrame& outFrame) const {
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) {
        return false;
    }
    const LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode < 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) {
        return false;
    }
    if (m_selectedRuntimeNode < static_cast<int>(model.nodeDeleted.size()) &&
        model.nodeDeleted[static_cast<size_t>(m_selectedRuntimeNode)]) {
        return false;
    }

    // 节点在归一化场景空间的世界矩阵（含场景归一化）。
    const Mat4 world = TransformMultiply(
        SceneNormalizationMatrix(),
        ModelSpaceNodeMatrix(model, m_selectedRuntimeNode));

    // 操作柄放在“选中节点子树”的包围盒中心（无几何时退回节点原点）。
    // 局部/世界轴的方向不变（仍取自节点自身的朝向或世界轴）。
    const Aabb subtreeBounds = NodeSubtreeWorldBounds(model, m_selectedRuntimeNode);
    outFrame.origin = AabbIsEmpty(subtreeBounds)
        ? Vec3{ world.m[3][0], world.m[3][1], world.m[3][2] }
        : AabbCenter(subtreeBounds);

    if (m_gizmoWorldSpace) {
        outFrame.axisX = Vec3{ 1.0f, 0.0f, 0.0f };
        outFrame.axisY = Vec3{ 0.0f, 1.0f, 0.0f };
        outFrame.axisZ = Vec3{ 0.0f, 0.0f, 1.0f };
    } else {
        // 局部模式：取世界矩阵旋转部分的三列（归一化）。
        auto column = [&world](int c) {
            Vec3 v{ world.m[c][0], world.m[c][1], world.m[c][2] };
            const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 1.0e-8f) { v.x /= len; v.y /= len; v.z /= len; }
            return v;
        };
        outFrame.axisX = column(0);
        outFrame.axisY = column(1);
        outFrame.axisZ = column(2);
    }

    // 固定屏幕尺寸：轴长 = 屏幕像素 * 每像素世界长度。
    Render* active = ActiveRender3D(m_renderer, m_openglRenderer, IsOpenGLBackend());
    if (!active) return false;
    const float worldPerPixel = active->GetSceneWorldPerPixel(outFrame.origin);
    if (!(worldPerPixel > 0.0f) || !std::isfinite(worldPerPixel)) return false;
    outFrame.worldPerPixel = worldPerPixel;
    outFrame.axisLength = kGizmoPixelLength * worldPerPixel;
    return true;
}

// 每帧把 Gizmo 线段几何提交给当前活动渲染器（无选中/无 Gizmo 时提交空）。
void MainWindow::UpdateGizmoGeometry() {
    Render* active = ActiveRender3D(m_renderer, m_openglRenderer, IsOpenGLBackend());
    if (!active || !active->IsInitialized()) return;

    GizmoFrame frame;
    if (!BuildGizmoFrame(frame)) {
        active->SetGizmoGeometry(nullptr, 0);
        return;
    }

    // 拖拽中高亮拖拽轴，否则高亮悬停轴。
    const GizmoAxis highlight = m_gizmoDragging ? m_gizmoDragAxis : m_gizmoHoverAxis;
    std::vector<GizmoVertex> geometry;
    BuildGizmoGeometry(frame, m_gizmoMode, highlight, geometry);
    if (geometry.empty()) {
        active->SetGizmoGeometry(nullptr, 0);
        return;
    }

    // 打包为交错 float：pos.xyz + color.rgba（每顶点 7 float）。
    std::vector<float> packed;
    packed.reserve(geometry.size() * 7);
    for (const GizmoVertex& v : geometry) {
        packed.push_back(v.position[0]);
        packed.push_back(v.position[1]);
        packed.push_back(v.position[2]);
        packed.push_back(v.color[0]);
        packed.push_back(v.color[1]);
        packed.push_back(v.color[2]);
        packed.push_back(v.color[3]);
    }
    active->SetGizmoGeometry(packed.data(), static_cast<uint32_t>(geometry.size()));
}

// 视口鼠标按下：优先尝试抓取 Gizmo 轴；返回 true 表示事件已被 Gizmo 消费。
bool MainWindow::TryBeginGizmoDrag(float nx, float ny) {
    if (m_gizmoDragging) return false;
    if (m_selectedModel < 0 || m_selectedRuntimeNode < 0) return false;

    GizmoFrame frame;
    if (!BuildGizmoFrame(frame)) return false;

    Render* active = ActiveRender3D(m_renderer, m_openglRenderer, IsOpenGLBackend());
    if (!active) return false;
    Vec3 origin{}, direction{};
    if (!active->GetSceneRay(nx, ny, origin, direction)) return false;
    const Ray ray{ origin, direction };

    const GizmoAxis axis = PickGizmoAxis(frame, m_gizmoMode, ray, kGizmoPickPixels);
    if (axis == GizmoAxis::None) return false;

    const LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    const RuntimeNode& node = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)];

    m_gizmoDragging = true;
    m_gizmoDragAxis = axis;
    m_gizmoDragFrame = frame;
    // 枢轴 = 操作柄位置（子树包围盒中心），换算到节点父空间（旋转/缩放补偿用）。
    m_gizmoDragPivotParent = ScenePointToParentSpace(model, m_selectedRuntimeNode, frame.origin);
    m_gizmoDragStartTranslation = node.local.translation;
    m_gizmoDragStartRotation = node.local.rotationDegrees;
    m_gizmoDragStartScale = node.local.scale;
    m_gizmoDragRecorded = false;

    if (m_gizmoMode == GizmoMode::Rotate) {
        float angle = 0.0f;
        m_gizmoDragStartAngle = GizmoRotateAngle(frame, axis, ray, angle) ? angle : 0.0f;
    } else {
        m_gizmoDragStartParam = GizmoAxisParam(frame, axis, ray);
    }
    return true;
}

// 视口鼠标拖动：Gizmo 拖拽中则应用变换并返回 true。
bool MainWindow::TryUpdateGizmoDrag(float nx, float ny) {
    if (!m_gizmoDragging) return false;
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) {
        return true;
    }
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode < 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) {
        return true;
    }
    RuntimeNode& node = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)];

    Render* active = ActiveRender3D(m_renderer, m_openglRenderer, IsOpenGLBackend());
    if (!active) return true;
    Vec3 origin{}, direction{};
    if (!active->GetSceneRay(nx, ny, origin, direction)) return true;
    const Ray ray{ origin, direction };

    const GizmoAxis axis = m_gizmoDragAxis;
    const float axisLength = m_gizmoDragFrame.axisLength > 1.0e-6f
        ? m_gizmoDragFrame.axisLength : 1.0f;
    const int axisIndex = (axis == GizmoAxis::X) ? 0 : (axis == GizmoAxis::Y ? 1 : 2);

    if (m_gizmoMode == GizmoMode::Translate) {
        const float param = GizmoAxisParam(m_gizmoDragFrame, axis, ray);
        float delta = param - m_gizmoDragStartParam;
        if (m_gizmoSnapEnabled && m_gizmoSnapTranslate) {
            const float step = static_cast<float>(m_gizmoSnapTranslate->value());
            if (step > 1.0e-6f) delta = std::round(delta / step) * step;
        }
        const Vec3 axisDir = GizmoAxisDirection(m_gizmoDragFrame, axis);
        const Vec3 deltaScene{ axisDir.x * delta, axisDir.y * delta, axisDir.z * delta };
        const Vec3 deltaParent = SceneVectorToParentSpace(model, m_selectedRuntimeNode, deltaScene);
        node.local.translation = Vec3{
            m_gizmoDragStartTranslation.x + deltaParent.x,
            m_gizmoDragStartTranslation.y + deltaParent.y,
            m_gizmoDragStartTranslation.z + deltaParent.z };
    } else if (m_gizmoMode == GizmoMode::Rotate) {
        float angle = 0.0f;
        if (!GizmoRotateAngle(m_gizmoDragFrame, axis, ray, angle)) return true;
        float delta = angle - m_gizmoDragStartAngle;
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        if (m_gizmoSnapEnabled && m_gizmoSnapRotate) {
            const float step = static_cast<float>(m_gizmoSnapRotate->value());
            if (step > 1.0e-6f) delta = std::round(delta / step) * step;
        }
        // 由起始局部旋转按“父世界旋转”组合得到新局部旋转（纯旋转，避免剪切）。
        const int parent = node.parent;
        const Mat4 parentModel = (parent >= 0)
            ? ModelSpaceNodeMatrix(model, parent) : TransformIdentityMatrix();
        const Transform parentRot = TransformFromMatrix(parentModel);
        const Mat4 parentR = TransformMatrixFromTrs(
            Vec3{ 0.0f, 0.0f, 0.0f }, parentRot.rotationDegrees, Vec3{ 1.0f, 1.0f, 1.0f });
        const Mat4 localR = TransformMatrixFromTrs(
            Vec3{ 0.0f, 0.0f, 0.0f }, m_gizmoDragStartRotation, Vec3{ 1.0f, 1.0f, 1.0f });

        Mat4 localRNew;
        if (m_gizmoWorldSpace) {
            const Vec3 axisDir = GizmoAxisDirection(m_gizmoDragFrame, axis);
            const Mat4 ra = TransformRotationAxisAngle(axisDir, delta);
            Mat4 parentRInv{};
            TransformInvert(parentR, parentRInv);
            localRNew = TransformMultiply(
                TransformMultiply(parentRInv, ra), TransformMultiply(parentR, localR));
        } else {
            // 局部模式：绕物体自身轴，右乘单位轴旋转。
            const Vec3 localAxis = (axis == GizmoAxis::X) ? Vec3{ 1.0f, 0.0f, 0.0f }
                : (axis == GizmoAxis::Y ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 0.0f, 0.0f, 1.0f });
            const Mat4 ra = TransformRotationAxisAngle(localAxis, delta);
            localRNew = TransformMultiply(localR, ra);
        }
        const Transform decomposed = TransformFromMatrix(localRNew);

        // 绕枢轴（子树包围盒中心）旋转：补偿平移，使枢轴保持不动。
        Transform startLocal;
        startLocal.translation = m_gizmoDragStartTranslation;
        startLocal.rotationDegrees = m_gizmoDragStartRotation;
        startLocal.scale = m_gizmoDragStartScale;
        Mat4 startInv{};
        TransformInvert(startLocal.ToMatrix(), startInv);
        const Vec3 pivotLocal = TransformPoint(startInv, m_gizmoDragPivotParent);
        const Mat4 rotScaleNew = TransformMatrixFromTrs(
            Vec3{ 0.0f, 0.0f, 0.0f }, decomposed.rotationDegrees, m_gizmoDragStartScale);
        const Vec3 pivotRotated = TransformVector(rotScaleNew, pivotLocal);
        node.local.rotationDegrees = decomposed.rotationDegrees;
        node.local.translation = GizmoVecSub(m_gizmoDragPivotParent, pivotRotated);
    } else {
        // 缩放：世界模式=等比（避免非轴对齐剪切），局部模式=单轴非等比。
        const float param = GizmoAxisParam(m_gizmoDragFrame, axis, ray);
        float ratio = 1.0f + (param - m_gizmoDragStartParam) / axisLength;
        if (!std::isfinite(ratio) || ratio < 1.0e-4f) ratio = 1.0e-4f;
        Vec3 scale = m_gizmoDragStartScale;
        if (m_gizmoWorldSpace) {
            if (m_gizmoSnapEnabled && m_gizmoSnapScale) {
                const float step = static_cast<float>(m_gizmoSnapScale->value());
                if (step > 1.0e-6f) {
                    const float base = std::max(std::fabs(m_gizmoDragStartScale.x), 1.0e-4f);
                    const float target = base * ratio;
                    ratio = (std::round(target / step) * step) / base;
                }
            }
            scale = Vec3{ m_gizmoDragStartScale.x * ratio,
                          m_gizmoDragStartScale.y * ratio,
                          m_gizmoDragStartScale.z * ratio };
        } else {
            // 局部模式：只改该轴分量（非等比缩放）。
            float sx = m_gizmoDragStartScale.x;
            float sy = m_gizmoDragStartScale.y;
            float sz = m_gizmoDragStartScale.z;
            float component = (axisIndex == 0 ? sx : (axisIndex == 1 ? sy : sz)) * ratio;
            if (m_gizmoSnapEnabled && m_gizmoSnapScale) {
                const float step = static_cast<float>(m_gizmoSnapScale->value());
                if (step > 1.0e-6f) component = std::round(component / step) * step;
            }
            if (std::fabs(component) < 1.0e-4f) component = 1.0e-4f;
            if (axisIndex == 0) sx = component;
            else if (axisIndex == 1) sy = component;
            else sz = component;
            scale = Vec3{ sx, sy, sz };
        }
        // 绕枢轴（子树包围盒中心）缩放：补偿平移，使枢轴保持不动。
        Transform startLocal;
        startLocal.translation = m_gizmoDragStartTranslation;
        startLocal.rotationDegrees = m_gizmoDragStartRotation;
        startLocal.scale = m_gizmoDragStartScale;
        Mat4 startInv{};
        TransformInvert(startLocal.ToMatrix(), startInv);
        const Vec3 pivotLocal = TransformPoint(startInv, m_gizmoDragPivotParent);
        const Mat4 rotScaleNew = TransformMatrixFromTrs(
            Vec3{ 0.0f, 0.0f, 0.0f }, m_gizmoDragStartRotation, scale);
        const Vec3 pivotScaled = TransformVector(rotScaleNew, pivotLocal);
        node.local.scale = scale;
        node.local.translation = GizmoVecSub(m_gizmoDragPivotParent, pivotScaled);
    }

    // 首次实际改动时记录一条撤销（整段拖拽合并为一条）。
    if (!m_gizmoDragRecorded) {
        TransformSnapshot snapshot;
        snapshot.model = m_selectedModel;
        snapshot.node = m_selectedRuntimeNode;
        snapshot.local.translation = m_gizmoDragStartTranslation;
        snapshot.local.rotationDegrees = m_gizmoDragStartRotation;
        snapshot.local.scale = m_gizmoDragStartScale;
        PushUndoSnapshot(snapshot, false);
        m_gizmoDragRecorded = true;
    }

    if (node.object) node.object->SetLocalTransform(node.local);
    SyncTransformPanelFromSelection();
    RefreshAfterTransformChange();
    return true;
}

// 视口鼠标移动（非拖拽）：更新 Gizmo 悬停轴（用于高亮）。
void MainWindow::UpdateGizmoHover(float nx, float ny) {
    m_gizmoHoverAxis = GizmoAxis::None;
    if (m_selectedModel < 0 || m_selectedRuntimeNode < 0) return;
    GizmoFrame frame;
    if (!BuildGizmoFrame(frame)) return;
    Render* active = ActiveRender3D(m_renderer, m_openglRenderer, IsOpenGLBackend());
    if (!active) return;
    Vec3 origin{}, direction{};
    if (!active->GetSceneRay(nx, ny, origin, direction)) return;
    const Ray ray{ origin, direction };
    m_gizmoHoverAxis = PickGizmoAxis(frame, m_gizmoMode, ray, kGizmoPickPixels);
}

// 视口鼠标松开：结束 Gizmo 拖拽（若有）。
void MainWindow::EndGizmoDrag() {
    if (!m_gizmoDragging) return;
    m_gizmoDragging = false;
    m_gizmoDragAxis = GizmoAxis::None;
    m_gizmoDragRecorded = false;
}

// 记录一条撤销记录（清空重做栈）；coalesce 为 true 时连续编辑合并为一条。
void MainWindow::PushUndoSnapshot(const TransformSnapshot& snapshot, bool coalesce) {
    if (snapshot.model < 0 || snapshot.node < 0) return;
    // 仅当上一笔记录与本次针对同一节点时，才把连续编辑合并为一条。
    const bool sameNodeAsTop = !m_undoStack.empty() &&
        m_undoStack.back().model == snapshot.model &&
        m_undoStack.back().node == snapshot.node;
    if (coalesce && m_undoCoalescing && sameNodeAsTop) {
        // 连续编辑同一节点：保留最早的一条快照，不重复入栈。
        m_redoStack.clear();
        return;
    }
    m_undoStack.push_back(snapshot);
    if (m_undoStack.size() > kMaxUndoSteps) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_undoCoalescing = coalesce;
    m_redoStack.clear();
}

// 把快照应用到场景并刷新面板 / 阴影。
void MainWindow::ApplyTransformSnapshot(const TransformSnapshot& snapshot) {
    if (snapshot.model < 0 || snapshot.model >= static_cast<int>(m_loadedModels.size())) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(snapshot.model)];
    if (snapshot.node < 0 || snapshot.node >= static_cast<int>(model.nodes.size())) return;
    RuntimeNode& node = model.nodes[static_cast<size_t>(snapshot.node)];
    node.local = snapshot.local;
    if (node.object) node.object->SetLocalTransform(node.local);
    // 面板显示当前选中节点；若撤销的是当前节点则同步。
    SyncTransformPanelFromSelection();
    RefreshAfterTransformChange();
}

// 任务 2.4：切换 Gizmo 模式。
void MainWindow::onGizmoTranslate() {
    m_gizmoMode = GizmoMode::Translate;
    m_gizmoHoverAxis = GizmoAxis::None;
    // 保证变换面板可见（坐标空间 / 吸附控件在其中）。
    if (m_transformPanel && !m_transformPanel->isVisible()) {
        m_transformPanel->setVisible(true);
        if (m_transformButton) {
            m_transformButton->setStyleSheet(
                QStringLiteral("background-color: #0078d4; color: #ffffff;"));
        }
        SyncTransformPanelFromSelection();
    }
    SyncGizmoUi();
}

void MainWindow::onGizmoRotate() {
    m_gizmoMode = GizmoMode::Rotate;
    m_gizmoHoverAxis = GizmoAxis::None;
    if (m_transformPanel && !m_transformPanel->isVisible()) {
        m_transformPanel->setVisible(true);
        if (m_transformButton) {
            m_transformButton->setStyleSheet(
                QStringLiteral("background-color: #0078d4; color: #ffffff;"));
        }
        SyncTransformPanelFromSelection();
    }
    SyncGizmoUi();
}

void MainWindow::onGizmoScale() {
    m_gizmoMode = GizmoMode::Scale;
    m_gizmoHoverAxis = GizmoAxis::None;
    if (m_transformPanel && !m_transformPanel->isVisible()) {
        m_transformPanel->setVisible(true);
        if (m_transformButton) {
            m_transformButton->setStyleSheet(
                QStringLiteral("background-color: #0078d4; color: #ffffff;"));
        }
        SyncTransformPanelFromSelection();
    }
    SyncGizmoUi();
}

// 任务 2.4：撤销上一次 Transform 修改。
void MainWindow::onUndoTransform() {
    if (m_undoStack.empty()) return;
    const TransformSnapshot snapshot = m_undoStack.back();
    m_undoStack.pop_back();
    // 把当前状态压入重做栈。
    if (snapshot.model >= 0 && snapshot.model < static_cast<int>(m_loadedModels.size())) {
        LoadedModel& model = m_loadedModels[static_cast<size_t>(snapshot.model)];
        if (snapshot.node >= 0 && snapshot.node < static_cast<int>(model.nodes.size())) {
            TransformSnapshot current;
            current.model = snapshot.model;
            current.node = snapshot.node;
            current.local = model.nodes[static_cast<size_t>(snapshot.node)].local;
            m_redoStack.push_back(current);
        }
    }
    ApplyTransformSnapshot(snapshot);
    m_undoCoalescing = false;
}

// 任务 2.4：重做上一次被撤销的 Transform 修改。
void MainWindow::onRedoTransform() {
    if (m_redoStack.empty()) return;
    const TransformSnapshot snapshot = m_redoStack.back();
    m_redoStack.pop_back();
    if (snapshot.model >= 0 && snapshot.model < static_cast<int>(m_loadedModels.size())) {
        LoadedModel& model = m_loadedModels[static_cast<size_t>(snapshot.model)];
        if (snapshot.node >= 0 && snapshot.node < static_cast<int>(model.nodes.size())) {
            TransformSnapshot current;
            current.model = snapshot.model;
            current.node = snapshot.node;
            current.local = model.nodes[static_cast<size_t>(snapshot.node)].local;
            m_undoStack.push_back(current);
        }
    }
    ApplyTransformSnapshot(snapshot);
    m_undoCoalescing = false;
}

// ---------------- 帮助菜单：关于 / 快捷键 ----------------

// 关于对话框：显示程序名称与版本号。
void MainWindow::onAbout() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("关于"));
    dialog.setModal(true);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    QLabel* title = new QLabel(QStringLiteral("DZC3DBrowser 1.0.0"));
    title->setStyleSheet(QStringLiteral("color: #d4d4d4; font-size: 15px; font-weight: bold; border: none;"));
    layout->addWidget(title);

    QLabel* description = new QLabel(QStringLiteral(
        "基于 Qt 5.12 + Vulkan 1.0 / OpenGL 4.2 的三维模型查看器。\n"
        "支持 OBJ / glTF 2.0 导入、材质与 PBR、HDR、阴影、\n"
        "对象变换 Gizmo 与场景树编辑。"));
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
    layout->addWidget(description);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

// 快捷键对话框：列出当前程序已有的快捷键。
void MainWindow::onShortcuts() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("快捷键"));
    dialog.setModal(true);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);

    QLabel* title = new QLabel(QStringLiteral("快捷键"));
    title->setStyleSheet(QStringLiteral("color: #d4d4d4; font-size: 15px; font-weight: bold; border: none;"));
    layout->addWidget(title);

    // 快捷键 -> 说明 列表（与代码中的 QShortcut 保持同步）。
    const std::vector<std::pair<QString, QString>> entries = {
        { QStringLiteral("W"),            QStringLiteral("切换到平移 Gizmo") },
        { QStringLiteral("E"),            QStringLiteral("切换到旋转 Gizmo") },
        { QStringLiteral("R"),            QStringLiteral("切换到缩放 Gizmo") },
        { QStringLiteral("Ctrl+Z"),       QStringLiteral("撤销上一次变换") },
        { QStringLiteral("Ctrl+Y"),       QStringLiteral("重做上一次变换") },
        { QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("重做上一次变换（备用）") },
    };

    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(6);
    int row = 0;
    for (const auto& entry : entries) {
        QLabel* key = new QLabel(entry.first);
        key->setStyleSheet(QStringLiteral("color: #569cd6; font-weight: bold; border: none;"));
        QLabel* desc = new QLabel(entry.second);
        desc->setStyleSheet(QStringLiteral("color: #d4d4d4; border: none;"));
        grid->addWidget(key, row, 0, Qt::AlignLeft);
        grid->addWidget(desc, row, 1, Qt::AlignLeft);
        ++row;
    }
    layout->addLayout(grid);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

// ---------------- 任务 2.3：选中高亮（由项目树驱动） ----------------

// 把高亮状态应用到场景对象（选中节点整棵子树高亮，其余清除）。
void MainWindow::ApplySelectionHighlight() {
    // 先清除全部网格对象的高亮。
    for (LoadedModel& model : m_loadedModels) {
        for (RuntimeNode& node : model.nodes) {
            for (RuntimeMesh& mesh : node.meshes) {
                if (mesh.object) mesh.object->SetHighlighted(false);
            }
        }
    }

    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) return;
    if (m_selectedRuntimeNode < 0) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) return;

    // 选中节点及其所有后代节点下的网格对象全部高亮（整个子树）。
    std::vector<int> stack{ m_selectedRuntimeNode };
    while (!stack.empty()) {
        const int r = stack.back();
        stack.pop_back();
        if (r < 0 || r >= static_cast<int>(model.nodes.size())) continue;
        if (r < static_cast<int>(model.nodeDeleted.size()) && model.nodeDeleted[r]) continue;
        for (RuntimeMesh& mesh : model.nodes[static_cast<size_t>(r)].meshes) {
            if (mesh.object) mesh.object->SetHighlighted(true);
        }
        for (int child : model.nodes[static_cast<size_t>(r)].children) {
            stack.push_back(child);
        }
    }
}

// 返回某运行时节点的显示名（模型根或无名时用模型文件名）。
QString MainWindow::NodeDisplayName(int modelIndex, int runtimeNode) const {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_loadedModels.size())) {
        return QString();
    }
    const LoadedModel& model = m_loadedModels[static_cast<size_t>(modelIndex)];
    QString name;
    if (runtimeNode >= 0 && runtimeNode < static_cast<int>(model.nodes.size())) {
        name = QString::fromStdString(model.nodes[static_cast<size_t>(runtimeNode)].name);
    }
    if (name.isEmpty()) {
        name = QFileInfo(QString::fromStdString(model.asset.sourcePath)).fileName();
    }
    return name;
}

// 选中节点：删除（含子树）。
void MainWindow::DeleteSelectedNode() {
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode <= 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) {
        ShowMessage(QStringLiteral("请选择一个子节点后再删除（模型根不可删除）。"), true);
        return;
    }
    MarkSubtreeDeleted(model, m_selectedRuntimeNode);
    m_selectedRuntimeNode = -1;
    SyncTransformPanelFromSelection();
    RebuildSceneMeshes(false);
    RebuildProjectTree();
    RefreshAfterTransformChange();
}

// 深拷贝某运行时节点子树；返回新根运行时索引。
int MainWindow::CloneRuntimeSubtree(LoadedModel& model, int srcRuntimeNode, int newParent,
                                    const Vec3& translationOffset) {
    if (srcRuntimeNode < 0 || srcRuntimeNode >= static_cast<int>(model.nodes.size())) return -1;

    // 先收集子树节点（DFS 顺序，父先于子）。
    std::vector<int> order;
    std::vector<int> stack;
    stack.push_back(srcRuntimeNode);
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        order.push_back(cur);
        const std::vector<int>& children = model.nodes[static_cast<size_t>(cur)].children;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push_back(*it);
        }
    }

    // 复制节点，建立 旧索引 -> 新索引 映射。
    std::unordered_map<int, int> remap;
    for (int oldIndex : order) {
        RuntimeNode copy = model.nodes[static_cast<size_t>(oldIndex)];
        copy.object = nullptr;
        copy.children.clear();
        copy.isClone = true;
        if (oldIndex == srcRuntimeNode) {
            copy.local.translation.x += translationOffset.x;
            copy.local.translation.y += translationOffset.y;
            copy.local.translation.z += translationOffset.z;
        }
        const int newIndex = static_cast<int>(model.nodes.size());
        remap[oldIndex] = newIndex;
        model.nodes.push_back(copy);
        model.nodeDeleted.push_back(0);
    }
    // 重连父子。
    for (int oldIndex : order) {
        const int newIndex = remap[oldIndex];
        for (int child : model.nodes[static_cast<size_t>(oldIndex)].children) {
            const auto found = remap.find(child);
            if (found != remap.end()) {
                model.nodes[static_cast<size_t>(newIndex)].children.push_back(found->second);
                model.nodes[static_cast<size_t>(found->second)].parent = newIndex;
            }
        }
    }
    // 挂到新父（或模型根）。
    const int newRoot = remap[srcRuntimeNode];
    const int parentRuntime =
        (newParent >= 0 && newParent < static_cast<int>(model.nodes.size())) ? newParent : 0;
    model.nodes[static_cast<size_t>(newRoot)].parent = parentRuntime;
    model.nodes[static_cast<size_t>(parentRuntime)].children.push_back(newRoot);
    return newRoot;
}

// 选中节点：复制（含子树）。
void MainWindow::CopySelectedNode() {
    if (m_selectedModel < 0 || m_selectedModel >= static_cast<int>(m_loadedModels.size())) return;
    LoadedModel& model = m_loadedModels[static_cast<size_t>(m_selectedModel)];
    if (m_selectedRuntimeNode < 0 ||
        m_selectedRuntimeNode >= static_cast<int>(model.nodes.size())) return;

    // 复制为同级节点（父不变）。
    const int parent = model.nodes[static_cast<size_t>(m_selectedRuntimeNode)].parent;
    const Vec3 offset{ 0.0f, 0.0f, 0.0f };
    const int newRoot = CloneRuntimeSubtree(model, m_selectedRuntimeNode, parent, offset);
    if (newRoot < 0) return;

    // 重建场景与项目树；选中新副本。
    RebuildSceneMeshes(false);
    RebuildProjectTree();
    m_selectedRuntimeNode = newRoot;
    // 在项目树中定位新副本项并选中。
    if (m_projectPanel) {
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* item)
            -> QTreeWidgetItem* {
            if (!item) return nullptr;
            const QVariant nd = item->data(0, Qt::UserRole + 1);
            if (nd.isValid() && nd.toInt() == newRoot) return item;
            for (int i = 0; i < item->childCount(); ++i) {
                if (QTreeWidgetItem* found = find(item->child(i))) return found;
            }
            return nullptr;
        };
        for (int i = 0; i < m_modelsTreeItem->childCount(); ++i) {
            if (QTreeWidgetItem* found = find(m_modelsTreeItem->child(i))) {
                m_projectPanel->setCurrentItem(found);
                break;
            }
        }
    }
    RefreshAfterTransformChange();
}

// 把模型上的网格指针置空。
void MainWindow::ResetLoadedMeshPointers() {
    for (LoadedModel& model : m_loadedModels) {
        model.meshes.clear();
        for (RuntimeNode& node : model.nodes) {
            node.object = nullptr;
            for (RuntimeMesh& mesh : node.meshes) {
                mesh.object = nullptr;
            }
        }
        // 旧渲染器即将销毁，其 GPU 纹理句柄随之失效。
        model.textureHandles.clear();
        model.texturesImported = false;
    }
}

// 按当前后端为所有已加载模型导入纹理（CPU 缓存 + GPU 上传）。
void MainWindow::ImportModelTextures() {
    if (!m_textureCache) return;

    Render* active = IsOpenGLBackend()
        ? static_cast<Render*>(m_openglRenderer)
        : static_cast<Render*>(m_renderer);
    if (!active || !active->IsInitialized() || active->IsDeviceLost()) return;
    // 只有三维后端才有材质/纹理语义；二维渲染器跳过。
    const bool is3D = IsOpenGLBackend()
        ? dynamic_cast<GLRender3D*>(m_openglRenderer) != nullptr
        : dynamic_cast<VKRender3D*>(m_renderer) != nullptr;
    if (!is3D) return;

    int totalReferenced = 0;
    int totalCreated = 0;
    int totalHits = 0;
    int totalFailed = 0;
    int totalSrgb = 0;
    int totalMips = 0;
    std::vector<std::string> notes;

    for (LoadedModel& model : m_loadedModels) {
        // 已为当前渲染器导入过的模型跳过（切换后端时 ResetLoadedMeshPointers 已清空）。
        if (model.texturesImported) continue;
        TextureImportResult imported =
            TextureImport::Import(model.asset, *m_textureCache, *active);
        model.textureHandles = std::move(imported.textureHandles);
        model.texturesImported = true;
        totalReferenced += imported.stats.referenced;
        totalCreated += imported.stats.created;
        totalHits += imported.stats.cacheHits;
        totalFailed += imported.stats.failed;
        totalSrgb += imported.stats.srgbCount;
        totalMips += imported.stats.totalMipLevels;
        for (const std::string& note : imported.stats.notes) {
            notes.push_back(note);
        }
    }

    TextureImportStats summary;
    summary.referenced = totalReferenced;
    summary.created = totalCreated;
    summary.cacheHits = totalHits;
    summary.failed = totalFailed;
    summary.srgbCount = totalSrgb;
    summary.totalMipLevels = totalMips;
    // 仅在确实涉及纹理时输出，避免普通导入刷屏。
    if (totalReferenced > 0) {
        ShowMessage(QString::fromStdString(TextureImport::BuildSummary(summary)), false);
    }
    for (const std::string& note : notes) {
        ShowMessage(QString::fromStdString(note), false);
    }
}

// 释放指定模型的 GPU 纹理引用（进入渲染器延迟销毁队列）。
void MainWindow::ReleaseModelTextures(LoadedModel& model) {
    if (model.textureHandles.empty()) return;
    Render* active = IsOpenGLBackend()
        ? static_cast<Render*>(m_openglRenderer)
        : static_cast<Render*>(m_renderer);
    if (active) {
        // 去重：不同纹理索引可能共享同一句柄（同一图像），只释放一次。
        std::vector<TextureHandle> unique;
        for (const auto& entry : model.textureHandles) {
            if (std::find(unique.begin(), unique.end(), entry.second) == unique.end()) {
                unique.push_back(entry.second);
            }
        }
        for (TextureHandle handle : unique) {
            active->DestroyTexture(handle);
        }
    }
    model.textureHandles.clear();
}

// 释放全部模型纹理引用。
void MainWindow::ReleaseAllModelTextures() {
    for (LoadedModel& model : m_loadedModels) {
        ReleaseModelTextures(model);
    }
}

// 销毁二维渲染器并换成三维。
void MainWindow::SwitchTo3D() {
    if (IsOpenGLBackend()) {
        SwitchOpenGLTo3D();
        return;
    }
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VKRender3D*>(m_renderer)) return;

    m_renderTimer->stop();
    ++m_loadGeneration;
    m_scene->Clear();
    ResetLoadedMeshPointers();
    m_renderer->Shutdown();
    delete m_renderer;

    VkInstance instance = m_vulkanWindow->GetVkInstance();
    VkSurfaceKHR surface = m_vulkanWindow->GetSurface();
    uint32_t w = static_cast<uint32_t>(m_vulkanWindow->width() * m_vulkanWindow->devicePixelRatio());
    uint32_t h = static_cast<uint32_t>(m_vulkanWindow->height() * m_vulkanWindow->devicePixelRatio());

    m_renderer = new VKRender3D();
    auto* vkRenderer = dynamic_cast<VKRender*>(m_renderer);
    ConfigureMeshFactory(m_renderer);
    if (!vkRenderer) return;
    m_vulkanWindow->SetRenderer(vkRenderer);

    vkRenderer->SetInstance(instance);
    vkRenderer->SetSurface(surface);
    m_renderer->SetFramebufferSize(w, h);
    if (m_renderer->Initialize("VulkanReference", w, h)) {
        auto* render3D = static_cast<VKRender3D*>(m_renderer);
        render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
        render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
        ApplyLightAnalysisToRenderer();
        ApplyPbrToRenderer();
        ApplyHdrToRenderer();
        ApplyDebugToRenderer();
        RebuildSceneMeshes();
        m_renderTimer->start(kRenderTickMs);
    } else {
        ReportRendererError(m_renderer, QStringLiteral("Vulkan 切换到三维失败"));
    }
    UpdateBackendStatus();
}

// 销毁三维渲染器并换成二维。
void MainWindow::SwitchTo2D() {
    if (IsOpenGLBackend()) {
        SwitchOpenGLTo2D();
        return;
    }
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VKRender2D*>(m_renderer)) return;

    m_renderTimer->stop();
    ++m_loadGeneration;
    m_scene->Clear();
    ResetLoadedMeshPointers();
    m_renderer->Shutdown();
    delete m_renderer;

    VkInstance instance = m_vulkanWindow->GetVkInstance();
    VkSurfaceKHR surface = m_vulkanWindow->GetSurface();
    uint32_t w = static_cast<uint32_t>(m_vulkanWindow->width() * m_vulkanWindow->devicePixelRatio());
    uint32_t h = static_cast<uint32_t>(m_vulkanWindow->height() * m_vulkanWindow->devicePixelRatio());

    m_renderer = new VKRender2D();
    auto* vkRenderer = dynamic_cast<VKRender*>(m_renderer);
    ConfigureMeshFactory(m_renderer);
    if (!vkRenderer) return;
    m_vulkanWindow->SetRenderer(vkRenderer);

    vkRenderer->SetInstance(instance);
    vkRenderer->SetSurface(surface);
    m_renderer->SetFramebufferSize(w, h);
    if (m_renderer->Initialize("VulkanReference", w, h)) {
        RebuildSceneMeshes();
        m_renderTimer->start(kRenderTickMs);
    } else {
        ReportRendererError(m_renderer, QStringLiteral("Vulkan 切换到二维失败"));
    }
    UpdateBackendStatus();
}

// 切到二维渲染器。
void MainWindow::on2DController() {
    SwitchTo2D();
}

// 切到三维渲染器。
void MainWindow::on3DController() {
    SwitchTo3D();
}

// 当前是否显示 OpenGL 视口。
bool MainWindow::IsOpenGLBackend() const {
    return m_openglContainer && m_openglContainer->isVisible();
}

// 在 Vulkan / OpenGL 后端间切换。
void MainWindow::onBackendEngineChanged(int index) {
    if (index == 1) {
        SwitchToOpenGL();
    } else {
        SwitchToVulkan();
    }
}

// 按需创建 OpenGL 窗口与容器。
void MainWindow::EnsureOpenGLWindow() {
    if (m_openglWindow && m_openglContainer) return;

    if (!m_openglRenderer) {
        m_openglRenderer = new GLRender3D();
    }
    auto* glRenderer = dynamic_cast<GLRender*>(m_openglRenderer);
    ConfigureMeshFactory(m_openglRenderer);
    if (!glRenderer) return;
    m_openglWindow = new QWindowOpenGL(glRenderer);
    m_openglContainer = QWidget::createWindowContainer(m_openglWindow);
    m_openglContainer->setMinimumSize(400, 300);
    m_openglContainer->setFocusPolicy(Qt::StrongFocus);
    m_openglWindow->installEventFilter(this);
    connect(m_openglWindow, &QWindowOpenGL::openGLReady, this, [this]() {
        EnsureOpenGLInitialized();
        RebuildSceneMeshes();
        StartRenderLoop();
    });

    if (m_viewportLayout) {
        m_viewportLayout->insertWidget(0, m_openglContainer, 1);
    }
    m_openglContainer->hide();
}

// 初始化 OpenGL 渲染器并同步显示选项。
void MainWindow::EnsureOpenGLInitialized() {
    if (!m_openglRenderer || !m_openglWindow) return;
    if (!m_openglWindow->GetContext()) return;

    if (!m_openglRenderer->IsInitialized()) {
        const uint32_t w = static_cast<uint32_t>(m_openglWindow->width() * m_openglWindow->devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(m_openglWindow->height() * m_openglWindow->devicePixelRatio());
        auto* glRenderer = dynamic_cast<GLRender*>(m_openglRenderer);
        if (!glRenderer) return;
        glRenderer->SetContext(m_openglWindow->GetContext());
        glRenderer->SetWindow(m_openglWindow);
        glRenderer->SetFramebufferSize(w, h);
        if (!m_openglRenderer->Initialize("OpenGLReference", w, h)) {
            ReportRendererError(m_openglRenderer, QStringLiteral("OpenGL 初始化失败"));
            return;
        }
    }

    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
        render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
        render3D->SetDyeEnabled(m_dyeCheck && m_dyeCheck->isChecked());
        render3D->SetOrthographicEnabled(m_orthographicCheck && m_orthographicCheck->isChecked());
        ApplyLightAnalysisToRenderer();
        ApplyPbrToRenderer();
        ApplyHdrToRenderer();
        ApplyDebugToRenderer();
    }
    RebuildSceneMeshes();
}

// 隐藏 Vulkan 视口，启用 OpenGL。
void MainWindow::SwitchToOpenGL() {
    EnsureOpenGLWindow();
    if (!m_openglContainer) return;
    if (m_openglContainer->isVisible()) return;

    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    ++m_loadGeneration;
    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_renderer->Shutdown();
    }

    if (m_container) m_container->hide();
    m_openglContainer->show();
    EnsureOpenGLInitialized();
    UpdateBackendStatus();
    StartRenderLoop();
}

// 隐藏 OpenGL 视口，恢复 Vulkan。
void MainWindow::SwitchToVulkan() {
    if (!m_openglContainer || !m_openglContainer->isVisible()) {
        return;
    }

    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    ++m_loadGeneration;
    if (m_openglRenderer && m_openglRenderer->IsInitialized()) {
        m_openglRenderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
    }
    if (m_openglContainer) m_openglContainer->hide();
    if (m_container) m_container->show();

    if (m_renderer && m_vulkanWindow && !m_renderer->IsInitialized()) {
        VkInstance instance = m_vulkanWindow->GetVkInstance();
        VkSurfaceKHR surface = m_vulkanWindow->GetSurface();
        const uint32_t w = static_cast<uint32_t>(m_vulkanWindow->width() * m_vulkanWindow->devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(m_vulkanWindow->height() * m_vulkanWindow->devicePixelRatio());
        auto* vkRenderer = dynamic_cast<VKRender*>(m_renderer);
        if (!vkRenderer) return;
        vkRenderer->SetInstance(instance);
        vkRenderer->SetSurface(surface);
        vkRenderer->SetFramebufferSize(w, h);
        if (m_renderer->Initialize("VulkanReference", w, h)) {
            if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
                render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
                render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
                render3D->SetDyeEnabled(m_dyeCheck && m_dyeCheck->isChecked());
                render3D->SetOrthographicEnabled(m_orthographicCheck && m_orthographicCheck->isChecked());
                ApplyLightAnalysisToRenderer();
                ApplyPbrToRenderer();
                ApplyHdrToRenderer();
                ApplyDebugToRenderer();
            }
            RebuildSceneMeshes();
        } else {
            ReportRendererError(m_renderer, QStringLiteral("Vulkan 重新初始化失败"));
        }
    }
    UpdateBackendStatus();
    StartRenderLoop();
}

// 把 OpenGL 后端换成三维。
void MainWindow::SwitchOpenGLTo3D() {
    if (!m_openglWindow) return;
    if (dynamic_cast<GLRender3D*>(m_openglRenderer)) return;

    if (m_renderTimer) m_renderTimer->stop();
    ++m_loadGeneration;
    if (m_openglRenderer) {
        if (m_openglRenderer->IsInitialized()) {
            m_openglRenderer->Quiesce();
        }
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
        delete m_openglRenderer;
        m_openglRenderer = nullptr;
    }

    m_openglRenderer = new GLRender3D();
    auto* glRenderer = dynamic_cast<GLRender*>(m_openglRenderer);
    ConfigureMeshFactory(m_openglRenderer);
    if (!glRenderer) return;
    m_openglWindow->SetRenderer(glRenderer);
    EnsureOpenGLInitialized();
    UpdateBackendStatus();
    StartRenderLoop();
}

// 把 OpenGL 后端换成二维。
void MainWindow::SwitchOpenGLTo2D() {
    if (!m_openglWindow) return;
    if (dynamic_cast<GLRender2D*>(m_openglRenderer)) return;

    if (m_renderTimer) m_renderTimer->stop();
    ++m_loadGeneration;
    if (m_openglRenderer) {
        if (m_openglRenderer->IsInitialized()) {
            m_openglRenderer->Quiesce();
        }
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
        delete m_openglRenderer;
        m_openglRenderer = nullptr;
    }

    m_openglRenderer = new GLRender2D();
    auto* glRenderer = dynamic_cast<GLRender*>(m_openglRenderer);
    ConfigureMeshFactory(m_openglRenderer);
    if (!glRenderer) return;
    m_openglWindow->SetRenderer(glRenderer);
    EnsureOpenGLInitialized();
    UpdateBackendStatus();
    StartRenderLoop();
}
