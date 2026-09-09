#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "QWindowOpenGL.h"
#include "Render/VulkanRender.h"
#include "Render/OpenGLRender.h"
#include "Layer/Layer.h"
#include "VulkanMesh.h"
#include "OpenGLMesh.h"
#include "ObjParseRunnable.h"
#include "Render/3DVulkanRender.h"
#include "Render/2DVulkanRender.h"
#include "Render/3DOpenGLRender.h"
#include "Render/2DOpenGLRender.h"
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
#include <QStyleFactory>
#include <QStyle>
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
#include <QMessageBox>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new VulkanRender3D())
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
    SetupToolBar();
    SetupVulkan();

    connect(m_borderCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
            render3D->SetWireframeEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
            render3D->SetWireframeEnabled(checked);
        }
    });

    connect(m_grayCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
            render3D->SetGrayEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
            render3D->SetGrayEnabled(checked);
        }
    });

    connect(m_dyeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
            render3D->SetDyeEnabled(checked);
        }
        if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
            render3D->SetDyeEnabled(checked);
        }
    });

    connect(m_orthographicCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
            render3D->SetOrthographicEnabled(checked);
            if (!checked) {
                render3D->ResetView(m_sceneViewDistance);
            }
        }
        if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
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

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->Quiesce();
        if (m_scene) m_scene->Clear();
        m_renderer->Shutdown();
    }
    if (m_openglRenderer && m_openglRenderer->IsInitialized()) {
        m_openglRenderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_openglRenderer->Shutdown();
    }
    QMainWindow::closeEvent(event);
}

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

void MainWindow::SetupStatusBar() {
    statusBar()->setSizeGripEnabled(false);

    m_coordX = new QLabel(QStringLiteral("X: 0.000"));
    m_coordY = new QLabel(QStringLiteral("Y: 0.000"));
    m_coordZ = new QLabel(QStringLiteral("Z: 0.000"));


    statusBar()->addPermanentWidget(m_coordX);
    statusBar()->addPermanentWidget(m_coordY);
    statusBar()->addPermanentWidget(m_coordZ);
}

void MainWindow::SetupVulkan() {
    m_vulkanWindow = new QWindowVulkan(m_renderer);
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

    connect(m_pComboTexSize, QOverload<const QString&>::of(&QComboBox::currentIndexChanged),
            this, [this](const QString&) {
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

    QSplitter* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setHandleWidth(1);
    hSplitter->addWidget(m_projectPanel);
    hSplitter->addWidget(viewportHost);
    hSplitter->setStretchFactor(0, 1);
    hSplitter->setStretchFactor(1, 2);

    QWidget* central = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(hSplitter);
    setCentralWidget(central);

    connect(m_vulkanWindow, &QWindowVulkan::vulkanReady, this, &MainWindow::StartRenderLoop);
}

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
            if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
                render3D->RequestCoordReadback(nx, ny);
            }
        } else if (m_renderer) {
            m_renderer->OnMouseMove(nx, ny);
            if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
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

void MainWindow::StartRenderLoop() {
    if (m_renderTimer) {
        m_renderTimer->start(16);
        return;
    }
    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, [this]() {
        if (IsOpenGLBackend()) {
            if (m_openglRenderer && m_openglRenderer->IsInitialized()) {
                if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
                    if (m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible() &&
                        render3D->IsSunAboveHorizon()) {
                        if (render3D->BeginShadowPass()) {
                            if (m_scene) m_scene->Render(SceneObject::RM_SHADOW);
                            render3D->EndShadowPass();
                        }
                    }
                }
                if (m_openglRenderer->BeginFrame()) {
                    if (m_scene) m_scene->Render(0);
                    m_openglRenderer->EndFrame();
                }
                if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
                    if (render3D->HasNewWorldCoord()) {
                        m_coordX->setText(QStringLiteral("X: %1").arg(render3D->GetLastWorldX(), 0, 'f', 3));
                        m_coordY->setText(QStringLiteral("Y: %1").arg(render3D->GetLastWorldY(), 0, 'f', 3));
                        m_coordZ->setText(QStringLiteral("Z: %1").arg(render3D->GetLastWorldZ(), 0, 'f', 3));
                    }
                }
            }
            return;
        }
        if (m_renderer && m_renderer->IsInitialized()) {
            if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
                if (m_lightAnalysisPanel && m_lightAnalysisPanel->isVisible() &&
                    render3D->IsSunAboveHorizon()) {
                    if (render3D->BeginShadowPass()) {
                        if (m_scene) m_scene->Render(SceneObject::RM_SHADOW);
                        render3D->EndShadowPass();
                    }
                }
            }
            if (m_renderer->BeginFrame()) {
                m_scene->Render(0);
                m_renderer->EndFrame();
            }
            if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
                if (render3D->HasNewWorldCoord()) {
                    m_coordX->setText(QStringLiteral("X: %1").arg(render3D->GetLastWorldX(), 0, 'f', 3));
                    m_coordY->setText(QStringLiteral("Y: %1").arg(render3D->GetLastWorldY(), 0, 'f', 3));
                    m_coordZ->setText(QStringLiteral("Z: %1").arg(render3D->GetLastWorldZ(), 0, 'f', 3));
                }
            }
        }
    });
    m_renderTimer->start(16);
}

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

    auto* vulkan3D = dynamic_cast<VulkanRender3D*>(m_renderer);
    auto* opengl3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer);
    applySun(vulkan3D);
    applySun(opengl3D);
    if (IsOpenGLBackend()) {
        applyShadow(opengl3D);
    } else {
        applyShadow(vulkan3D);
    }
    UpdateShadowSceneBounds();
}

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
        for (const Vertex3D& vertex : model.sourceVertices) {
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

    if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
        render3D->SetShadowSceneBounds(bboxMin, bboxMax, anyVisible);
    }
    if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
        render3D->SetShadowSceneBounds(bboxMin, bboxMax, anyVisible);
    }
}

void MainWindow::SyncShadowTextureSizeCombo(uint32_t size) {
    if (!m_pComboTexSize) return;
    const QString text = QString::number(size);
    if (m_pComboTexSize->currentText() == text) return;
    const int index = m_pComboTexSize->findText(text);
    if (index < 0) return;
    const QSignalBlocker blocker(m_pComboTexSize);
    m_pComboTexSize->setCurrentIndex(index);
}

void MainWindow::ShowShadowMapStatus(const std::string& message) {
    if (message.empty()) return;
    statusBar()->showMessage(QString::fromStdString(message), 8000);
}

void MainWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择 OBJ 文件"),
        "",
        QStringLiteral("OBJ 文件 (*.obj)"));

    if (filePath.isEmpty()) return;
    LoadFile(filePath);
}

void MainWindow::LoadFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
            QStringLiteral("无法打开文件:\n%1").arg(filePath));
        return;
    }

    const uint64_t loadGeneration = m_loadGeneration;

    auto* runnable = new ObjParseRunnable(
        filePath.toStdString(),
        [this, filePath, loadGeneration](std::vector<Vertex3D>&& vertices,
                                         std::vector<uint32_t>&& indices,
                                         const Vec3& /*sourceCenter*/,
                                         float /*normalizationScale*/) {
            if (loadGeneration != m_loadGeneration) return;
            if (!m_renderer || m_renderer->IsShuttingDown()) return;
            AddLoadedModel(filePath, std::move(vertices), std::move(indices));
        });

    QThreadPool::globalInstance()->start(runnable);
}

void MainWindow::onRecentFileTriggered() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    LoadFile(action->data().toString());
}

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

void MainWindow::LoadRecentFiles() {
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    while (m_recentFiles.size() > 5) {
        m_recentFiles.removeLast();
    }
}

void MainWindow::SaveRecentFiles() {
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
}

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

void MainWindow::AddLoadedModel(const QString& filePath,
                                std::vector<Vertex3D>&& vertices,
                                std::vector<uint32_t>&& indices) {
    if (vertices.empty() || indices.empty()) return;

    LoadedModel model;
    model.sourceVertices = std::move(vertices);
    model.indices = std::move(indices);
    model.treeItem = new QTreeWidgetItem(m_modelsTreeItem);
    model.treeItem->setText(0, QFileInfo(filePath).fileName());
    m_modelsTreeItem->setExpanded(true);

    m_loadedModels.push_back(std::move(model));
    RebuildSceneMeshes();
    AddRecentFile(filePath);
}

void MainWindow::SetLoadedModelVisible(QTreeWidgetItem* treeItem, bool visible) {
    auto it = std::find_if(m_loadedModels.begin(), m_loadedModels.end(),
        [treeItem](const LoadedModel& model) {
            return model.treeItem == treeItem;
        });
    if (it == m_loadedModels.end() || it->visible == visible) return;

    it->visible = visible;
    if (it->mesh) {
        it->mesh->SetVisible(visible);
    }
    UpdateShadowSceneBounds();
}

void MainWindow::RemoveLoadedModel(QTreeWidgetItem* treeItem) {
    auto it = std::find_if(m_loadedModels.begin(), m_loadedModels.end(),
        [treeItem](const LoadedModel& model) {
            return model.treeItem == treeItem;
        });
    if (it == m_loadedModels.end()) return;

    if (it->mesh && m_scene) {
        if (m_renderer && m_renderer->IsInitialized()) {
            m_renderer->WaitForIdle();
        }
        m_scene->RemoveChild(it->mesh);
        it->mesh = nullptr;
    }

    delete it->treeItem;
    it->treeItem = nullptr;
    m_loadedModels.erase(it);
    UpdateShadowSceneBounds();
}

void MainWindow::ClearLoadedModels() {
    ++m_loadGeneration;

    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->WaitForIdle();
    }

    for (LoadedModel& model : m_loadedModels) {
        if (model.mesh && m_scene) {
            m_scene->RemoveChild(model.mesh);
            model.mesh = nullptr;
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

    if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetCoordinateNormalization(Vec3{}, 1.0f);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
    if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
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
        for (const Vertex3D& vertex : model.sourceVertices) {
            bboxMin.x = std::min(bboxMin.x, vertex.position[0]);
            bboxMin.y = std::min(bboxMin.y, vertex.position[1]);
            bboxMin.z = std::min(bboxMin.z, vertex.position[2]);
            bboxMax.x = std::max(bboxMax.x, vertex.position[0]);
            bboxMax.y = std::max(bboxMax.y, vertex.position[1]);
            bboxMax.z = std::max(bboxMax.z, vertex.position[2]);
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

    if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(normalizedCenter);
        render3D->ResetView(viewDistance);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
    if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
        render3D->SetOrbitCenter(normalizedCenter);
        render3D->ResetView(viewDistance);
        render3D->SetOrthographicEnabled(
            m_orthographicCheck && m_orthographicCheck->isChecked());
    }
}

void MainWindow::RebuildSceneMeshes() {
    const bool useOpenGL = IsOpenGLBackend()
        && dynamic_cast<OpenGLRender3D*>(m_openglRenderer) != nullptr;
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
        for (const Vertex3D& vertex : model.sourceVertices) {
            bboxMin.x = std::min(bboxMin.x, vertex.position[0]);
            bboxMin.y = std::min(bboxMin.y, vertex.position[1]);
            bboxMin.z = std::min(bboxMin.z, vertex.position[2]);
            bboxMax.x = std::max(bboxMax.x, vertex.position[0]);
            bboxMax.y = std::max(bboxMax.y, vertex.position[1]);
            bboxMax.z = std::max(bboxMax.z, vertex.position[2]);
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
    if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetOrthographicEnabled(orthographicEnabled);
        render3D->SetCoordinateNormalization(center, scale);
    }
    if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetOrthographicEnabled(orthographicEnabled);
        render3D->SetCoordinateNormalization(center, scale);
    }

    if (!useOpenGL && m_renderer) {
        m_renderer->WaitForIdle();
    }

    for (LoadedModel& model : m_loadedModels) {
        std::vector<Vertex3D> normalizedVertices = model.sourceVertices;
        for (Vertex3D& vertex : normalizedVertices) {
            vertex.position[0] = (vertex.position[0] - center.x) * scale;
            vertex.position[1] = (vertex.position[1] - center.y) * scale;
            vertex.position[2] = (vertex.position[2] - center.z) * scale;
        }

        if (useOpenGL) {
            auto* glMesh = dynamic_cast<OpenGLMesh*>(model.mesh);
            if (!glMesh) {
                glMesh = new OpenGLMesh();
                glMesh->SetRender(m_openglRenderer);
                glMesh->SetVisible(model.visible);
                model.mesh = glMesh;
                m_scene->AddChild(glMesh);
            }
            glMesh->SetMeshDataSync(normalizedVertices, model.indices);
        } else {
            auto* vkMesh = dynamic_cast<VulkanMesh*>(model.mesh);
            if (!vkMesh) {
                vkMesh = new VulkanMesh();
                vkMesh->SetRender(m_renderer);
                vkMesh->SetVisible(model.visible);
                model.mesh = vkMesh;
                m_scene->AddChild(vkMesh);
            }
            auto indices = model.indices;
            vkMesh->SetMeshData(std::move(normalizedVertices), std::move(indices));
        }
    }
    UpdateShadowSceneBounds();
}

void MainWindow::ResetLoadedMeshPointers() {
    for (LoadedModel& model : m_loadedModels) {
        model.mesh = nullptr;
    }
}

void MainWindow::SwitchTo3D() {
    if (IsOpenGLBackend()) {
        SwitchOpenGLTo3D();
        return;
    }
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VulkanRender3D*>(m_renderer)) return;

    m_renderTimer->stop();
    m_scene->Clear();
    ResetLoadedMeshPointers();
    m_renderer->Shutdown();
    delete m_renderer;

    VkInstance instance = m_vulkanWindow->GetVkInstance();
    VkSurfaceKHR surface = m_vulkanWindow->GetSurface();
    uint32_t w = static_cast<uint32_t>(m_vulkanWindow->width() * m_vulkanWindow->devicePixelRatio());
    uint32_t h = static_cast<uint32_t>(m_vulkanWindow->height() * m_vulkanWindow->devicePixelRatio());

    m_renderer = new VulkanRender3D();
    m_vulkanWindow->SetRenderer(m_renderer);

    m_renderer->SetInstance(instance);
    m_renderer->SetSurface(surface);
    m_renderer->SetFramebufferSize(w, h);
    if (m_renderer->Initialize("VulkanReference", w, h)) {
        auto* render3D = static_cast<VulkanRender3D*>(m_renderer);
        render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
        render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
        ApplyLightAnalysisToRenderer();
        RebuildSceneMeshes();
        m_renderTimer->start(16);
    }
}

void MainWindow::SwitchTo2D() {
    if (IsOpenGLBackend()) {
        SwitchOpenGLTo2D();
        return;
    }
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VulkanRender2D*>(m_renderer)) return;

    m_renderTimer->stop();
    m_scene->Clear();
    ResetLoadedMeshPointers();
    m_renderer->Shutdown();
    delete m_renderer;

    VkInstance instance = m_vulkanWindow->GetVkInstance();
    VkSurfaceKHR surface = m_vulkanWindow->GetSurface();
    uint32_t w = static_cast<uint32_t>(m_vulkanWindow->width() * m_vulkanWindow->devicePixelRatio());
    uint32_t h = static_cast<uint32_t>(m_vulkanWindow->height() * m_vulkanWindow->devicePixelRatio());

    m_renderer = new VulkanRender2D();
    m_vulkanWindow->SetRenderer(m_renderer);

    m_renderer->SetInstance(instance);
    m_renderer->SetSurface(surface);
    m_renderer->SetFramebufferSize(w, h);
    if (m_renderer->Initialize("VulkanReference", w, h)) {
        RebuildSceneMeshes();
        m_renderTimer->start(16);
    }
}

void MainWindow::on2DController() {
    SwitchTo2D();
}

void MainWindow::on3DController() {
    SwitchTo3D();
}

bool MainWindow::IsOpenGLBackend() const {
    return m_openglContainer && m_openglContainer->isVisible();
}

void MainWindow::onBackendEngineChanged(int index) {
    if (index == 1) {
        SwitchToOpenGL();
    } else {
        SwitchToVulkan();
    }
}

void MainWindow::EnsureOpenGLWindow() {
    if (m_openglWindow && m_openglContainer) return;

    if (!m_openglRenderer) {
        m_openglRenderer = new OpenGLRender3D();
    }
    m_openglWindow = new QWindowOpenGL(m_openglRenderer);
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

void MainWindow::EnsureOpenGLInitialized() {
    if (!m_openglRenderer || !m_openglWindow) return;
    if (!m_openglWindow->GetContext()) return;

    if (!m_openglRenderer->IsInitialized()) {
        const uint32_t w = static_cast<uint32_t>(m_openglWindow->width() * m_openglWindow->devicePixelRatio());
        const uint32_t h = static_cast<uint32_t>(m_openglWindow->height() * m_openglWindow->devicePixelRatio());
        m_openglRenderer->SetContext(m_openglWindow->GetContext());
        m_openglRenderer->SetWindow(m_openglWindow);
        m_openglRenderer->SetFramebufferSize(w, h);
        if (!m_openglRenderer->Initialize("OpenGLReference", w, h)) return;
    }

    if (auto* render3D = dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) {
        render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
        render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
        render3D->SetDyeEnabled(m_dyeCheck && m_dyeCheck->isChecked());
        render3D->SetOrthographicEnabled(m_orthographicCheck && m_orthographicCheck->isChecked());
        ApplyLightAnalysisToRenderer();
    }
    RebuildSceneMeshes();
}

void MainWindow::SwitchToOpenGL() {
    EnsureOpenGLWindow();
    if (!m_openglContainer) return;
    if (m_openglContainer->isVisible()) return;

    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->Quiesce();
        if (m_scene) m_scene->Clear();
        ResetLoadedMeshPointers();
        m_renderer->Shutdown();
    }

    if (m_container) m_container->hide();
    m_openglContainer->show();
    EnsureOpenGLInitialized();
    StartRenderLoop();
}

void MainWindow::SwitchToVulkan() {
    if (!m_openglContainer || !m_openglContainer->isVisible()) {
        return;
    }

    if (m_renderTimer) {
        m_renderTimer->stop();
    }
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
        m_renderer->SetInstance(instance);
        m_renderer->SetSurface(surface);
        m_renderer->SetFramebufferSize(w, h);
        if (m_renderer->Initialize("VulkanReference", w, h)) {
            if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
                render3D->SetGrayEnabled(m_grayCheck && m_grayCheck->isChecked());
                render3D->SetWireframeEnabled(m_borderCheck && m_borderCheck->isChecked());
                render3D->SetDyeEnabled(m_dyeCheck && m_dyeCheck->isChecked());
                render3D->SetOrthographicEnabled(m_orthographicCheck && m_orthographicCheck->isChecked());
                ApplyLightAnalysisToRenderer();
            }
            RebuildSceneMeshes();
        }
    }
    StartRenderLoop();
}

void MainWindow::SwitchOpenGLTo3D() {
    if (!m_openglWindow) return;
    if (dynamic_cast<OpenGLRender3D*>(m_openglRenderer)) return;

    if (m_renderTimer) m_renderTimer->stop();
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

    m_openglRenderer = new OpenGLRender3D();
    m_openglWindow->SetRenderer(m_openglRenderer);
    EnsureOpenGLInitialized();
    StartRenderLoop();
}

void MainWindow::SwitchOpenGLTo2D() {
    if (!m_openglWindow) return;
    if (dynamic_cast<OpenGLRender2D*>(m_openglRenderer)) return;

    if (m_renderTimer) m_renderTimer->stop();
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

    m_openglRenderer = new OpenGLRender2D();
    m_openglWindow->SetRenderer(m_openglRenderer);
    EnsureOpenGLInitialized();
    StartRenderLoop();
}
