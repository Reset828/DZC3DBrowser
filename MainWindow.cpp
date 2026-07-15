#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "render/VulkanRender.h"
#include "VulkanLayer.h"
#include "VulkanMesh.h"
#include "ObjParseRunnable.h"
#include "render/3DVulkanRender.h"
#include "render/2DVulkanRender.h"
#include <QToolBar>
#include <QAction>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
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
#include <QPlainTextEdit>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new VulkanRender3D())
    , m_vulkanWindow(nullptr)
    , m_container(nullptr)
    , m_scene(new VulkanLayer())
    , m_renderTimer(nullptr)
{
    resize(1280, 720);
    SetupToolBar();
    SetupVulkan();

    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background-color: #1e1e1e; }
        QWidget { background-color: #1e1e1e; color: #d4d4d4; }
        QTreeWidget { background-color: #252526; color: #d4d4d4; border: none; outline: none; }
        QTreeWidget::item:hover { background-color: #2a2d2e; }
        QTreeWidget::item:selected { background-color: #094771; }
        QHeaderView::section { background-color: #2d2d2d; color: #d4d4d4; border: none; padding: 4px; }
        QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: none; }
        QSplitter::handle { background-color: #3c3c3c; }
        QToolBar { background-color: #2d2d2d; border: none; spacing: 4px; padding: 2px; }
        QToolButton { background-color: transparent; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 3px; padding: 4px 12px; }
        QToolButton:hover { background-color: #3c3c3c; }
        QToolButton:pressed { background-color: #094771; }
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
    QToolBar* toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);

    QToolButton* fileBtn = new QToolButton();
    fileBtn->setText(QStringLiteral("文件"));
    fileBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* fileMenu = new QMenu(fileBtn);
    fileMenu->addAction(QStringLiteral("打开文件"), this, &MainWindow::onOpenFile);
    fileBtn->setMenu(fileMenu);
    toolbar->addWidget(fileBtn);

    QToolButton* viewBtn = new QToolButton();
    viewBtn->setText(QStringLiteral("查看"));
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* viewMenu = new QMenu(viewBtn);
    viewMenu->addAction(QStringLiteral("3D查看"), this, &MainWindow::on3DController);
    viewMenu->addAction(QStringLiteral("2D查看"), this, &MainWindow::on2DController);
    viewBtn->setMenu(viewMenu);
    toolbar->addWidget(viewBtn);
}

void MainWindow::SetupVulkan() {
    m_vulkanWindow = new QWindowVulkan(m_renderer);
    m_container = QWidget::createWindowContainer(m_vulkanWindow);
    m_container->setMinimumSize(400, 300);
    m_container->setFocusPolicy(Qt::StrongFocus);
    m_vulkanWindow->installEventFilter(this);

    m_projectPanel = new QTreeWidget();
    m_projectPanel->setHeaderLabel(QStringLiteral("工程"));
    m_projectPanel->setMinimumWidth(150);

    m_outputWindow = new QPlainTextEdit();
    m_outputWindow->setReadOnly(true);
    m_outputWindow->setPlaceholderText(QStringLiteral("输出"));
    m_outputWindow->setMaximumBlockCount(1000);
    m_outputWindow->setMinimumHeight(60);

    QSplitter* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setHandleWidth(1);
    hSplitter->addWidget(m_projectPanel);
    hSplitter->addWidget(m_container);
    hSplitter->setStretchFactor(0, 1);
    hSplitter->setStretchFactor(1, 2);

    QSplitter* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->setHandleWidth(1);
    vSplitter->addWidget(hSplitter);
    vSplitter->addWidget(m_outputWindow);
    vSplitter->setStretchFactor(0, 4);
    vSplitter->setStretchFactor(1, 1);

    QWidget* central = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(vSplitter);
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
        }
    });
    m_renderTimer->start(16);
}

void MainWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择 OBJ 文件"),
        QStringLiteral("./obj"),
        QStringLiteral("OBJ 文件 (*.obj)"));

    if (filePath.isEmpty()) return;

    m_scene->Clear();

    auto* runnable = new ObjParseRunnable(
        filePath.toStdString(),
        [this](std::vector<Vertex3D>&& vertices,
               std::vector<uint32_t>&& indices) {
            if (!m_renderer || m_renderer->IsShuttingDown()) return;

            m_storedVertices = vertices;
            m_storedIndices = indices;
            m_hasStoredMesh = true;

            auto* mesh = new VulkanMesh();
            mesh->SetRender(m_renderer);
            mesh->SetMeshData(std::move(vertices), std::move(indices));
            m_scene->AddChild(mesh);
        });


    QThreadPool::globalInstance()->start(runnable);
}

void MainWindow::SwitchTo3D() {
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VulkanRender3D*>(m_renderer)) return;

    m_renderTimer->stop();
    m_scene->Clear();
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
        if (m_hasStoredMesh) {
            auto verts = m_storedVertices;
            auto idxs = m_storedIndices;
            auto* mesh = new VulkanMesh();
            mesh->SetRender(m_renderer);
            mesh->SetMeshData(std::move(verts), std::move(idxs));
            m_scene->AddChild(mesh);
        }
        m_renderTimer->start(16);
    }
}

void MainWindow::SwitchTo2D() {
    if (!m_renderer || !m_vulkanWindow) return;
    if (dynamic_cast<VulkanRender2D*>(m_renderer)) return;

    m_renderTimer->stop();
    m_scene->Clear();
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
        if (m_hasStoredMesh) {
            auto verts = m_storedVertices;
            auto idxs = m_storedIndices;
            auto* mesh = new VulkanMesh();
            mesh->SetRender(m_renderer);
            mesh->SetMeshData(std::move(verts), std::move(idxs));
            m_scene->AddChild(mesh);
        }
        m_renderTimer->start(16);
    }
}

void MainWindow::on2DController() {
    SwitchTo2D();
}

void MainWindow::on3DController() {
    SwitchTo3D();
}
