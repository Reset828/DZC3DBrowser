#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include <QMainWindow>
#include <vector>
#include <cstdint>

class QWindowVulkan;
class VulkanRender;
class VulkanLayer;
class QTimer;
class QWidget;
class QTreeWidget;
class QPlainTextEdit;
class QCheckBox;
class QLabel;
struct Vertex3D;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onOpenFile();
    void on2DController();
    void on3DController();

private:
    void SetupToolBar();
    void SetupStatusBar();
    void SetupVulkan();
    void StartRenderLoop();
    void SwitchTo2D();
    void SwitchTo3D();

    VulkanRender* m_renderer;
    QWindowVulkan* m_vulkanWindow;
    QWidget* m_container;
    VulkanLayer* m_scene;
    QTimer* m_renderTimer;
    QTreeWidget* m_projectPanel;
    QPlainTextEdit* m_outputWindow;

    std::vector<Vertex3D> m_storedVertices;
    std::vector<uint32_t> m_storedIndices;
    bool m_hasStoredMesh = false;
    float m_meshSourceCenter[3] = {};
    float m_meshNormalizationScale = 1.0f;

    QCheckBox* m_borderCheck = nullptr;
    QCheckBox* m_grayCheck = nullptr;

    QLabel* m_coordX = nullptr;
    QLabel* m_coordY = nullptr;
    QLabel* m_coordZ = nullptr;
};

#endif // __MAIN_WINDOW_H__
