#ifndef __QWINDOW_VULKAN_H__
#define __QWINDOW_VULKAN_H__
#define VK_USE_PLATFORM_WIN32_KHR 

#include <QWindow>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h> 

class VulkanRender;

class QWindowVulkan : public QWindow {
    Q_OBJECT
public:
    explicit QWindowVulkan(VulkanRender* renderer);
    ~QWindowVulkan();

    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkInstance GetVkInstance() const { return m_instance; }
    void SetRenderer(VulkanRender* renderer) { m_renderer = renderer; }

signals:
    void vulkanReady();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool CreateVulkanSurface();

    VulkanRender* m_renderer;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    bool m_initialized;
};

#endif // __QWINDOW_VULKAN_H__
