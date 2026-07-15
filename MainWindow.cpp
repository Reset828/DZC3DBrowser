#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "VulkanRender.h"
#include "VulkanLayer.h"
#include "VulkanMesh.h"
#include "ObjParseRunnable.h"
#include <QToolBar>
#include <QAction>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QCloseEvent>
#include <QThreadPool>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new VulkanRender())
    , m_vulkanWindow(nullptr)
    , m_scene(new VulkanLayer())
    , m_renderTimer(nullptr)
{
    resize(1280, 720);
    SetupToolBar();
    SetupVulkan();
}

MainWindow::~MainWindow() {
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    // 先销毁场景（释放 mesh 的 Vulkan 缓冲区），此时 renderer 仍然有效
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
        // 1) 停止新异步任务，等待进行中的任务完成（device 仍然有效）
        m_renderer->Quiesce();

        // 2) 销毁所有 mesh 的 Vulkan 缓冲区（此时 device 有效，m_pRender 有效）
        if (m_scene) m_scene->Clear();

        // 3) 完全关闭渲染器（销毁 device 等其余 Vulkan 资源）
        m_renderer->Shutdown();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::SetupToolBar() {
    QToolBar* toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);

    QAction* actOpen = toolbar->addAction(QStringLiteral("打开文件"));
    QAction* act2D = toolbar->addAction(QStringLiteral("二维控制器"));
    QAction* act3D = toolbar->addAction(QStringLiteral("三维控制器"));

    connect(actOpen, &QAction::triggered, this, &MainWindow::onOpenFile);
    connect(act2D, &QAction::triggered, this, &MainWindow::on2DController);
    connect(act3D, &QAction::triggered, this, &MainWindow::on3DController);
}

void MainWindow::SetupVulkan() {
    m_vulkanWindow = new QWindowVulkan(m_renderer);
    QWidget* container = QWidget::createWindowContainer(m_vulkanWindow);
    container->setMinimumSize(400, 300);
    container->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(container);

    connect(m_vulkanWindow, &QWindowVulkan::vulkanReady, this, &MainWindow::StartRenderLoop);
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

    // 在后台线程中解析 OBJ 文件
    auto* runnable = new ObjParseRunnable(
        filePath.toStdString(),
        [this](std::vector<VulkanVertex>&& vertices,
               std::vector<uint32_t>&& indices) {
            // 关闭中不回调
            if (!m_renderer || m_renderer->IsShuttingDown()) return;

            auto* mesh = new VulkanMesh();
            mesh->SetRender(m_renderer);
            mesh->SetMeshData(std::move(vertices), std::move(indices));
            m_scene->AddChild(mesh);
        });


    QThreadPool::globalInstance()->start(runnable);
}

void MainWindow::on2DController() {
    QMessageBox::information(this, QStringLiteral("提示"),
        QStringLiteral("二维控制器待实现"));
}

void MainWindow::on3DController() {
    QMessageBox::information(this, QStringLiteral("提示"),
        QStringLiteral("三维控制器待实现"));
}