#include "MainWindow.h"
#include "QWindowVulkan.h"
#include "VulkanRender.h"
#include "VulkanLayer.h"
#include <QToolBar>
#include <QAction>
#include <QWidget>
#include <QMessageBox>
#include <QTimer>
#include <QCloseEvent>

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
    if (m_renderer) {
        m_renderer->Shutdown();
        delete m_renderer;
    }
    delete m_scene;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
    if (m_renderer && m_renderer->IsInitialized()) {
        m_renderer->Shutdown();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::SetupToolBar() {
    QToolBar* toolbar = addToolBar(QStringLiteral("\u5de5\u5177\u680f"));
    toolbar->setMovable(false);

    QAction* actOpen = toolbar->addAction(QStringLiteral("\u6253\u5f00\u6587\u4ef6"));
    QAction* act2D = toolbar->addAction(QStringLiteral("\u4e8c\u7ef4\u63a7\u5236\u5668"));
    QAction* act3D = toolbar->addAction(QStringLiteral("\u4e09\u7ef4\u63a7\u5236\u5668"));

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
    QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
        QStringLiteral("\u6253\u5f00\u6587\u4ef6\u529f\u80fd\u5f85\u5b9e\u73b0"));
}

void MainWindow::on2DController() {
    QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
        QStringLiteral("\u4e8c\u7ef4\u63a7\u5236\u5668\u5f85\u5b9e\u73b0"));
}

void MainWindow::on3DController() {
    QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
        QStringLiteral("\u4e09\u7ef4\u63a7\u5236\u5668\u5f85\u5b9e\u73b0"));
}
