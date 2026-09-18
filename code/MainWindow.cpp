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
#include <QStyleFactory>
#include <QStyle>
#include <QColor>
#include <QBrush>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDial>
#include <QDate>
#include <QTime>
#include <QtGlobal>
#include <QSignalBlocker>
#include <QSettings>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <limits>
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

    SetupStatusBar();

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

    m_lightAnalysisButton = new QPushButton(QStringLiteral("光照分析"));
    toolbar->addWidget(m_lightAnalysisButton);
    connect(m_lightAnalysisButton, &QPushButton::clicked, this, &MainWindow::onLightAnalysis);

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

        if (item->parent() != m_modelsTreeItem) return;

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
    });

    connect(m_projectPanel, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int /*column*/) {
        if (!item) return;

        if (item == m_modelsTreeItem) {
            FocusSceneOrModel(nullptr);
            return;
        }

        if (item->parent() == m_modelsTreeItem) {
            FocusSceneOrModel(item);
        }
    });

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

    QWidget* viewportHost = new QWidget();
    QHBoxLayout* viewportLayout = new QHBoxLayout(viewportHost);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);
    m_viewportLayout = viewportLayout;
    viewportLayout->addWidget(m_container, 1);
    viewportLayout->addWidget(m_lightAnalysisPanel, 0);

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
    m_messageSplitter->addWidget(m_messageList);
    m_messageSplitter->setStretchFactor(0, 1);
    m_messageSplitter->setStretchFactor(1, 0);
    m_messageSplitter->setSizes({ 600, 120 });

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

// 清空消息区并逐条显示导入诊断。
void MainWindow::ShowImportMessages(const std::vector<ImportMessage>& messages) {
    if (!m_messageList) return;
    m_messageList->clear();
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
        dispatchDown(nx, ny, btn);
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(event);
        int btn = -1;
        if (me->button() == Qt::LeftButton) btn = 0;
        else if (me->button() == Qt::RightButton) btn = 1;
        else if (me->button() == Qt::MiddleButton) btn = 2;
        dispatchUp(btn);
        break;
    }
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);
        float nx = (float)me->x() / (float)window->width();
        float ny = (float)me->y() / (float)window->height();
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

// 把光照面板参数写进当前三维渲染器。
void MainWindow::ApplyLightAnalysisToRenderer() {
    auto applySun = [this](auto* render3D) {
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
    auto applyShadow = [this](auto* render3D) {
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
    if (IsOpenGLBackend()) {
        applyShadow(opengl3D);
    } else {
        applyShadow(vulkan3D);
    }
    UpdateShadowSceneBounds();
    // 光照参数变化后让阴影通道立即重画一次，锁定新的光源矩阵。
    m_lastShadowPassTimer.invalidate();
}

// 用已加载模型包围盒更新阴影范围。
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
        for (const MeshData& mesh : model.asset.meshes) {
            for (const AssetVertex& vertex : mesh.vertices) {
                const float x = (vertex.position[0] - m_sceneSourceCenter[0]) * m_sceneNormalizationScale;
                const float y = (vertex.position[1] - m_sceneSourceCenter[1]) * m_sceneNormalizationScale;
                const float z = (vertex.position[2] - m_sceneSourceCenter[2]) * m_sceneNormalizationScale;
                bboxMin.x = std::min(bboxMin.x, x);
                bboxMin.y = std::min(bboxMin.y, y);
                bboxMin.z = std::min(bboxMin.z, z);
                bboxMax.x = std::max(bboxMax.x, x);
                bboxMax.y = std::max(bboxMax.y, y);
                bboxMax.z = std::max(bboxMax.z, z);
                anyVisible = true;
            }
        }
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
    RebuildSceneMeshes();
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

    if (!it->meshes.empty() && m_scene) {
        if (m_renderer && m_renderer->IsInitialized()) {
            m_renderer->WaitForIdle();
        }
        for (Object* mesh : it->meshes) {
            if (mesh) m_scene->RemoveChild(mesh);
        }
        it->meshes.clear();
    }
    // 释放该模型的 GPU 纹理（延迟销毁）。
    ReleaseModelTextures(*it);

    delete it->treeItem;
    it->treeItem = nullptr;
    m_loadedModels.erase(it);
    UpdateShadowSceneBounds();
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
        if (!model.meshes.empty() && m_scene) {
            for (Object* mesh : model.meshes) {
                if (mesh) m_scene->RemoveChild(mesh);
            }
            model.meshes.clear();
        }
        delete model.treeItem;
        model.treeItem = nullptr;
    }
    m_loadedModels.clear();

    m_sceneViewDistance = 3.0f;
    m_sceneSourceCenter[0] = 0.0f;
    m_sceneSourceCenter[1] = 0.0f;
    m_sceneSourceCenter[2] = 0.0f;
    m_sceneNormalizationScale = 1.0f;

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
    UpdateShadowSceneBounds();
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

    Vec3 bboxMin = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    Vec3 bboxMax = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };

    auto includeModelBounds = [&](const LoadedModel& model) {
        for (const MeshData& mesh : model.asset.meshes) {
            for (const AssetVertex& vertex : mesh.vertices) {
                bboxMin.x = std::min(bboxMin.x, vertex.position[0]);
                bboxMin.y = std::min(bboxMin.y, vertex.position[1]);
                bboxMin.z = std::min(bboxMin.z, vertex.position[2]);
                bboxMax.x = std::max(bboxMax.x, vertex.position[0]);
                bboxMax.y = std::max(bboxMax.y, vertex.position[1]);
                bboxMax.z = std::max(bboxMax.z, vertex.position[2]);
            }
        }
    };

    if (selectedModel) {
        includeModelBounds(*selectedModel);
    } else {
        for (const LoadedModel& model : m_loadedModels) {
            includeModelBounds(model);
        }
    }

    const Vec3 sourceCenter = {
        (bboxMin.x + bboxMax.x) * 0.5f,
        (bboxMin.y + bboxMax.y) * 0.5f,
        (bboxMin.z + bboxMax.z) * 0.5f
    };
    const Vec3 normalizedCenter = {
        (sourceCenter.x - m_sceneSourceCenter[0]) * m_sceneNormalizationScale,
        (sourceCenter.y - m_sceneSourceCenter[1]) * m_sceneNormalizationScale,
        (sourceCenter.z - m_sceneSourceCenter[2]) * m_sceneNormalizationScale
    };

    const float sizeX = (bboxMax.x - bboxMin.x) * m_sceneNormalizationScale;
    const float sizeY = (bboxMax.y - bboxMin.y) * m_sceneNormalizationScale;
    const float sizeZ = (bboxMax.z - bboxMin.z) * m_sceneNormalizationScale;
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
    const float fitDistance = sizeZ * 0.5f +
        std::max(sizeY * 0.5f / tanVertical,
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

// 按当前后端重建场景网格。
void MainWindow::RebuildSceneMeshes() {
    const bool useOpenGL = IsOpenGLBackend()
        && dynamic_cast<GLRender3D*>(m_openglRenderer) != nullptr;
    if (useOpenGL) {
        if (!m_openglRenderer || !m_openglRenderer->IsInitialized() || m_loadedModels.empty()) return;
    } else if (IsOpenGLBackend()) {
        return;
    } else {
        if (!m_renderer || !m_renderer->IsInitialized() || m_loadedModels.empty()) return;
    }

    Vec3 bboxMin = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    Vec3 bboxMax = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };

    for (const LoadedModel& model : m_loadedModels) {
        for (const MeshData& mesh : model.asset.meshes) {
            for (const AssetVertex& vertex : mesh.vertices) {
                bboxMin.x = std::min(bboxMin.x, vertex.position[0]);
                bboxMin.y = std::min(bboxMin.y, vertex.position[1]);
                bboxMin.z = std::min(bboxMin.z, vertex.position[2]);
                bboxMax.x = std::max(bboxMax.x, vertex.position[0]);
                bboxMax.y = std::max(bboxMax.y, vertex.position[1]);
                bboxMax.z = std::max(bboxMax.z, vertex.position[2]);
            }
        }
    }

    const Vec3 center = {
        (bboxMin.x + bboxMax.x) * 0.5f,
        (bboxMin.y + bboxMax.y) * 0.5f,
        (bboxMin.z + bboxMax.z) * 0.5f
    };
    const float sizeX = bboxMax.x - bboxMin.x;
    const float sizeY = bboxMax.y - bboxMin.y;
    const float sizeZ = bboxMax.z - bboxMin.z;
    const float maxSize = std::max({ sizeX, sizeY, sizeZ });
    const float scale = maxSize > std::numeric_limits<float>::epsilon()
        ? 2.0f / maxSize
        : 1.0f;
    m_sceneSourceCenter[0] = center.x;
    m_sceneSourceCenter[1] = center.y;
    m_sceneSourceCenter[2] = center.z;
    m_sceneNormalizationScale = scale;

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
    const float halfX = sizeX * scale * 0.5f;
    const float halfY = sizeY * scale * 0.5f;
    const float halfZ = sizeZ * scale * 0.5f;
    const float fitDistance = halfZ +
        std::max(halfY / tanVertical, halfX / tanHorizontal) + 0.1f;
    m_sceneViewDistance = std::max(3.0f, fitDistance);

    const bool orthographicEnabled =
        m_orthographicCheck && m_orthographicCheck->isChecked();
    if (auto* render3D = dynamic_cast<VKRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetOrthographicEnabled(orthographicEnabled);
        render3D->SetCoordinateNormalization(center, scale);
    }
    if (auto* render3D = dynamic_cast<GLRender3D*>(m_openglRenderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetOrthographicEnabled(orthographicEnabled);
        render3D->SetCoordinateNormalization(center, scale);
    }

    if (!useOpenGL && m_renderer) {
        m_renderer->WaitForIdle();
    }

    GLRender* glRenderer = useOpenGL ? dynamic_cast<GLRender*>(m_openglRenderer) : nullptr;
    VKRender* vkRenderer = useOpenGL ? nullptr : dynamic_cast<VKRender*>(m_renderer);
    if ((useOpenGL && !glRenderer) || (!useOpenGL && !vkRenderer)) return;

    for (LoadedModel& model : m_loadedModels) {
        // 归一化资产顶点坐标（不改原始资产，复制一份用于上传）。
        // 每个 MeshData 一个 Mesh 对象；节点变换已在解析时烘焙。
        const size_t meshCount = model.asset.meshes.size();
        model.meshes.resize(meshCount, nullptr);

        for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
            MeshData normalized = model.asset.meshes[meshIndex];
            for (AssetVertex& vertex : normalized.vertices) {
                vertex.position[0] = (vertex.position[0] - center.x) * scale;
                vertex.position[1] = (vertex.position[1] - center.y) * scale;
                vertex.position[2] = (vertex.position[2] - center.z) * scale;
            }

            if (useOpenGL) {
                auto* glMesh = dynamic_cast<GLMesh*>(model.meshes[meshIndex]);
                if (!glMesh) {
                    Object* mesh = m_openglRenderer->CreateMesh();
                    glMesh = dynamic_cast<GLMesh*>(mesh);
                    if (!glMesh) {
                        delete mesh;
                        continue;
                    }
                    glMesh->SetVisible(model.visible);
                    model.meshes[meshIndex] = glMesh;
                    m_scene->AddChild(glMesh);
                }
                glMesh->SetMeshDataSync(normalized);
            } else {
                auto* vkMesh = dynamic_cast<VKMesh*>(model.meshes[meshIndex]);
                if (!vkMesh) {
                    Object* mesh = m_renderer->CreateMesh();
                    vkMesh = dynamic_cast<VKMesh*>(mesh);
                    if (!vkMesh) {
                        delete mesh;
                        continue;
                    }
                    vkMesh->SetVisible(model.visible);
                    model.meshes[meshIndex] = vkMesh;
                    m_scene->AddChild(vkMesh);
                }
                vkMesh->SetMeshData(normalized);
            }
        }
    }
    // 网格就绪后导入纹理（CPU 缓存 + 当前后端 GPU 上传）。
    ImportModelTextures();
    UpdateShadowSceneBounds();
}

// 把模型上的网格指针置空。
void MainWindow::ResetLoadedMeshPointers() {
    for (LoadedModel& model : m_loadedModels) {
        model.meshes.clear();
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
