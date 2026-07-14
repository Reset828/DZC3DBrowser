#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include <QMainWindow>

class QWindowVulkan;
class VulkanRender;
class VulkanLayer;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenFile();
    void on2DController();
    void on3DController();

private:
    void SetupToolBar();
    void SetupVulkan();
    void StartRenderLoop();

    VulkanRender* m_renderer;
    QWindowVulkan* m_vulkanWindow;
    VulkanLayer* m_scene;
    QTimer* m_renderTimer;
};

#endif // __MAIN_WINDOW_H__
