#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <string>
#include <vector>

#define LOG_TAG "MyLayer"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define VK_LAYER_EXPORT __attribute__((visibility("default")))


static PFN_vkQueuePresentKHR real_vkQueuePresentKHR = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL my_vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    ALOGI("Swapchain image presented! Image count: %u", pPresentInfo->swapchainCount);


    return real_vkQueuePresentKHR(queue, pPresentInfo);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL my_vkGetInstanceProcAddr(
    VkInstance instance, const char* pName)
{
    if (!strcmp(pName, "vkQueuePresentKHR")) {
        if (!real_vkQueuePresentKHR) {
            real_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetInstanceProcAddr(instance, "vkQueuePresentKHR");
        }
        return (PFN_vkVoidFunction)my_vkQueuePresentKHR;
    }
    
    return vkGetInstanceProcAddr(instance, pName); 
}


extern "C" {

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return my_vkGetInstanceProcAddr(instance, pName);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    return VK_SUCCESS;
}

}