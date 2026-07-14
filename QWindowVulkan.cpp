#include "QWindowVulkan.h"
#include "VulkanRender.h"
#include <QExposeEvent>
#include <QResizeEvent>
#include <windows.h>

QWindowVulkan::QWindowVulkan(VulkanRender* renderer)
    : m_renderer(renderer)
    , m_instance(VK_NULL_HANDLE)
    , m_surface(VK_NULL_HANDLE)
    , m_initialized(false)
{
    setSurfaceType(QSurface::VulkanSurface);
}

QWindowVulkan::~QWindowVulkan() {
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void QWindowVulkan::exposeEvent(QExposeEvent* event) {
    if (isExposed() && !m_initialized) {
        m_initialized = true;

        if (!CreateVulkanSurface()) return;

        uint32_t w = static_cast<uint32_t>(width() * devicePixelRatio());
        uint32_t h = static_cast<uint32_t>(height() * devicePixelRatio());

        m_renderer->SetInstance(m_instance);
        m_renderer->SetSurface(m_surface);
        m_renderer->SetFramebufferSize(w, h);
        m_renderer->Initialize("VulkanReference", w, h);

        emit vulkanReady();
    }
}

void QWindowVulkan::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    if (m_renderer->IsInitialized()) {
        uint32_t w = static_cast<uint32_t>(event->size().width() * devicePixelRatio());
        uint32_t h = static_cast<uint32_t>(event->size().height() * devicePixelRatio());
        m_renderer->SetFramebufferSize(w, h);
        m_renderer->SetFramebufferResized(true);
    }
}

bool QWindowVulkan::CreateVulkanSurface() {
    auto extensions = VulkanRender::GetRequiredExtensions();

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VulkanReference";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkanEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&instInfo, nullptr, &m_instance) != VK_SUCCESS) {
        return false;
    }

    VkWin32SurfaceCreateInfoKHR surfInfo{};
    surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfInfo.hinstance = GetModuleHandle(nullptr);
    surfInfo.hwnd = (HWND)winId();

    if (vkCreateWin32SurfaceKHR(m_instance, &surfInfo, nullptr, &m_surface) != VK_SUCCESS) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    return true;
}
