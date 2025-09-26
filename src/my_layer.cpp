// my_layer.cpp

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <string.h>
#include <mutex>
#include <unordered_map>

#define LOG_TAG "MyVulkanLayer"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#define VK_LAYER_API_VERSION 2

// 전역 디스패치 테이블
static std::unordered_map<void*, PFN_vkVoidFunction> g_device_dispatch_table;
static std::mutex g_dispatch_mutex;

// 다음 레이어의 vkGetInstanceProcAddr 함수 포인터
static PFN_vkGetInstanceProcAddr g_next_vkGetInstanceProcAddr = nullptr;

// vkQueuePresentKHR 후킹
VKAPI_ATTR VkResult VKAPI_CALL my_vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    ALOGI("Swapchain image presented! Image count: %u", pPresentInfo->swapchainCount);

    std::lock_guard<std::mutex> lock(g_dispatch_mutex);
    auto it = g_device_dispatch_table.find(reinterpret_cast<void*>(queue));
    PFN_vkQueuePresentKHR next = nullptr;
    if (it != g_device_dispatch_table.end()) {
        next = reinterpret_cast<PFN_vkQueuePresentKHR>(it->second);
    }
    if (next) {
        return next(queue, pPresentInfo);
    }
    return VK_SUCCESS;
}

// vkGetDeviceProcAddr 후킹
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL my_vkGetDeviceProcAddr(
    VkDevice device, const char* pName)
{
    std::lock_guard<std::mutex> lock(g_dispatch_mutex);

    if (!strcmp(pName, "vkQueuePresentKHR")) {
        return reinterpret_cast<PFN_vkVoidFunction>(my_vkQueuePresentKHR);
    }

    auto it = g_device_dispatch_table.find(reinterpret_cast<void*>(device));
    PFN_vkGetDeviceProcAddr next = nullptr;
    if (it != g_device_dispatch_table.end()) {
        next = reinterpret_cast<PFN_vkGetDeviceProcAddr>(it->second);
    }

    return next ? next(device, pName) : nullptr;
}

// vkCreateDevice 후킹
VKAPI_ATTR VkResult VKAPI_CALL my_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    std::lock_guard<std::mutex> lock(g_dispatch_mutex);

    PFN_vkCreateDevice next_vkCreateDevice = nullptr;
    if (g_next_vkGetInstanceProcAddr) {
        next_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
            g_next_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice")
        );
    }

    if (!next_vkCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = next_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    // vkGetDeviceProcAddr 저장
    PFN_vkGetDeviceProcAddr next_vkGetDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            g_next_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetDeviceProcAddr")
        );

    g_device_dispatch_table[reinterpret_cast<void*>(*pDevice)] =
        reinterpret_cast<PFN_vkVoidFunction>(next_vkGetDeviceProcAddr);

    // Queue 후킹 준비
    VkQueue queue;
    vkGetDeviceQueue(*pDevice, 0, 0, &queue);

    PFN_vkQueuePresentKHR next_vkQueuePresentKHR =
    reinterpret_cast<PFN_vkQueuePresentKHR>(
        next_vkGetDeviceProcAddr(*pDevice, "vkQueuePresentKHR")
    );
    g_device_dispatch_table[reinterpret_cast<void*>(queue)] =
        reinterpret_cast<PFN_vkVoidFunction>(next_vkQueuePresentKHR);

    return VK_SUCCESS;
}

// Vulkan Loader Layer 진입점
extern "C" {

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* pName)
{
    ALOGI("vkGetInstanceProcAddr called");
    if (!g_next_vkGetInstanceProcAddr) {
        g_next_vkGetInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr(instance, pName));
    }

    if (!strcmp(pName, "vkGetDeviceProcAddr")) {
        return reinterpret_cast<PFN_vkVoidFunction>(my_vkGetDeviceProcAddr);
    }

    if (!strcmp(pName, "vkCreateDevice")) {
        return reinterpret_cast<PFN_vkVoidFunction>(my_vkCreateDevice);
    }

    return g_next_vkGetInstanceProcAddr(instance, pName);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct)
{
    ALOGI("vkNegotiateLoaderLayerInterfaceVersion called");
    
    if (pVersionStruct->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = my_vkGetDeviceProcAddr;
    pVersionStruct->loaderLayerInterfaceVersion = VK_LAYER_API_VERSION;

    return VK_SUCCESS;
}

} // extern "C"
