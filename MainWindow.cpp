#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "render/VulkanRender.h"
#include "VulkanLayer.h"
#include "VulkanMesh.h"
#include "ObjParseRunnable.h"
#include "render/3DVulkanRender.h"
#include "render/2DVulkanRender.h"
#include <QString>
#include <QAction>
#include <QMenuBar>
#include <QToolBar>
#include <QCheckBox>
#include <QLabel>
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
#include <algorithm>
#include <cmath>
#include <limits>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new VulkanRender3D())
    , m_vulkanWindow(nullptr)
    , m_container(nullptr)
    , m_scene(new VulkanLayer())
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
    });

    connect(m_grayCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
            render3D->SetGrayEnabled(checked);
        }
    });

    connect(m_dyeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
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
    });

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
    delete m_scene;
    m_scene = nullptr;

    if (m_renderer) {
        m_renderer->Shutdown();
        delete m_renderer;
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
    QMainWindow::closeEvent(event);
}

void MainWindow::SetupToolBar() {
    QMenuBar* mb = menuBar();

    QMenu* fileMenu = mb->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(QStringLiteral("打开文件"), this, &MainWindow::onOpenFile);

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

    QSplitter* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setHandleWidth(1);
    hSplitter->addWidget(m_projectPanel);
    hSplitter->addWidget(m_container);
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
    if (obj == m_vulkanWindow && m_renderer) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            float nx = (float)me->x() / (float)m_vulkanWindow->width();
            float ny = (float)me->y() / (float)m_vulkanWindow->height();
            int btn = -1;
            if (me->button() == Qt::LeftButton) btn = 0;
            else if (me->button() == Qt::RightButton) btn = 1;
            else if (me->button() == Qt::MiddleButton) btn = 2;
            m_renderer->OnMouseDown(nx, ny, btn);
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(event);
            int btn = -1;
            if (me->button() == Qt::LeftButton) btn = 0;
            else if (me->button() == Qt::RightButton) btn = 1;
            else if (me->button() == Qt::MiddleButton) btn = 2;
            m_renderer->OnMouseUp(btn);
            break;
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(event);
            float nx = (float)me->x() / (float)m_vulkanWindow->width();
            float ny = (float)me->y() / (float)m_vulkanWindow->height();
            m_renderer->OnMouseMove(nx, ny);
            if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
                render3D->RequestCoordReadback(nx, ny);
            }
            break;
        }
        case QEvent::Wheel: {
            auto* we = static_cast<QWheelEvent*>(event);
            m_renderer->OnMouseWheel((float)we->angleDelta().y());
            break;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::StartRenderLoop() {
    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, [this]() {
        if (m_renderer->IsInitialized()) {
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

void MainWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择 OBJ 文件"),
        "",
        QStringLiteral("OBJ 文件 (*.obj)"));

    if (filePath.isEmpty()) return;

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

    m_coordX->setText(QStringLiteral("X: 0.000"));
    m_coordY->setText(QStringLiteral("Y: 0.000"));
    m_coordZ->setText(QStringLiteral("Z: 0.000"));
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
    const float aspect = static_cast<float>(std::max(1, m_vulkanWindow->width())) /
                         static_cast<float>(std::max(1, m_vulkanWindow->height()));
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
}

void MainWindow::RebuildSceneMeshes() {
    if (!m_renderer || !m_renderer->IsInitialized() || m_loadedModels.empty()) return;

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

    const float aspect = static_cast<float>(std::max(1, m_vulkanWindow->width())) /
                         static_cast<float>(std::max(1, m_vulkanWindow->height()));
    constexpr float verticalHalfFov = 0.3926990817f; // 45° / 2
    const float tanVertical = std::tan(verticalHalfFov);
    const float tanHorizontal = tanVertical * aspect;
    const float halfX = sizeX * scale * 0.5f;
    const float halfY = sizeY * scale * 0.5f;
    const float halfZ = sizeZ * scale * 0.5f;
    const float fitDistance = halfZ +
        std::max(halfY / tanVertical, halfX / tanHorizontal) + 0.1f;
    m_sceneViewDistance = std::max(3.0f, fitDistance);

    if (auto* render3D = dynamic_cast<VulkanRender3D*>(m_renderer)) {
        const bool orthographicEnabled =
            m_orthographicCheck && m_orthographicCheck->isChecked();
        render3D->SetOrbitCenter(Vec3{});
        render3D->ResetView(m_sceneViewDistance);
        render3D->SetOrthographicEnabled(orthographicEnabled);
        render3D->SetCoordinateNormalization(center, scale);
    }

    m_renderer->WaitForIdle();

    for (LoadedModel& model : m_loadedModels) {
        std::vector<Vertex3D> normalizedVertices = model.sourceVertices;
        for (Vertex3D& vertex : normalizedVertices) {
            vertex.position[0] = (vertex.position[0] - center.x) * scale;
            vertex.position[1] = (vertex.position[1] - center.y) * scale;
            vertex.position[2] = (vertex.position[2] - center.z) * scale;
        }

        if (!model.mesh) {
            model.mesh = new VulkanMesh();
            model.mesh->SetRender(m_renderer);
            model.mesh->SetVisible(model.visible);
            m_scene->AddChild(model.mesh);
        }

        auto indices = model.indices;
        model.mesh->SetMeshData(std::move(normalizedVertices), std::move(indices));
    }
}

void MainWindow::ResetLoadedMeshPointers() {
    for (LoadedModel& model : m_loadedModels) {
        model.mesh = nullptr;
    }
}

void MainWindow::SwitchTo3D() {
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
        RebuildSceneMeshes();
        m_renderTimer->start(16);
    }
}

void MainWindow::SwitchTo2D() {
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
