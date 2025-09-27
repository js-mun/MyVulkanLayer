#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>

#include <string.h>
#include <unordered_map>
#include <mutex>
#include <memory>

// Android 로깅 매크로 정의
#define LOG_TAG "VulkanLayerSample"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- 데이터 관리 및 스레드 안전성 ---

struct LayerInstanceData {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr next_pfnGetInstanceProcAddr;
    PFN_vkDestroyInstance next_pfnDestroyInstance;
    PFN_vkGetDeviceProcAddr next_pfnGetDeviceProcAddr;
};

// 스레드 안전성을 위한 뮤텍스
static std::mutex g_instance_mutex;
// VkInstance와 레이어 데이터를 매핑하는 해시맵
static std::unordered_map<void*, std::unique_ptr<LayerInstanceData>> g_instance_data_map;


// --- 훅된 Vulkan 함수 구현 ---

VKAPI_ATTR void VKAPI_CALL Hook_vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator)
{
    void* dispatch_key = *(void**)instance;
    std::unique_ptr<LayerInstanceData> layer_data;
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        auto it = g_instance_data_map.find(dispatch_key);
        if (it != g_instance_data_map.end()) {
            layer_data = std::move(it->second);
            g_instance_data_map.erase(it);
        }
    }

    if (layer_data) {
        ALOGI("Hook_vkDestroyInstance! handle: %p", (void*)instance);
        layer_data->next_pfnDestroyInstance(instance, pAllocator);
    } else {
        ALOGE("Hook_vkDestroyInstance: unknown instance.");
    }
}


VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    ALOGI("Hook_vkCreateInstance called");

    VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;

    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                               layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = (VkLayerInstanceCreateInfo*)layerCreateInfo->pNext;
    }

    if (!layerCreateInfo) {
        ALOGE("Not found VK_LAYER_LINK_INFO.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_pfnGetInstanceProcAddr = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    
    PFN_vkCreateInstance next_pfnCreateInstance = (PFN_vkCreateInstance)
        next_pfnGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");

    if (!next_pfnCreateInstance) {
        ALOGE("Can't get vkCreateInstance func pointer");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
    VkResult result = next_pfnCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) {
        ALOGE("Failed to call vkCreateInstance of Next layer/driver: VkResult %d", result);
        return result;
    }

    auto layer_data = std::make_unique<LayerInstanceData>();
    layer_data->instance = *pInstance;
    layer_data->next_pfnGetInstanceProcAddr = next_pfnGetInstanceProcAddr;
    
    // 다음 레이어의 다른 함수 포인터들은 next_pfnGetInstanceProcAddr를 통해 가져옵니다.
    layer_data->next_pfnDestroyInstance = (PFN_vkDestroyInstance)next_pfnGetInstanceProcAddr(*pInstance, "vkDestroyInstance");
    
    layer_data->next_pfnGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)next_pfnGetInstanceProcAddr(*pInstance, "vkGetDeviceProcAddr");


    void* dispatch_key = *(void**)*pInstance;
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        g_instance_data_map[dispatch_key] = std::move(layer_data);
    }

    ALOGI("vkCreateInstance called successfully. handle: %p", (void*)*pInstance);
    if (pCreateInfo && pCreateInfo->pApplicationInfo) {
        const VkApplicationInfo* appInfo = pCreateInfo->pApplicationInfo;
        ALOGI("  App name: %s (API Version: 0x%X)",
              appInfo->pApplicationName ? appInfo->pApplicationName : "N/A",
              appInfo->apiVersion);
    }

    return VK_SUCCESS;
}

// --- 로더 인터페이스 함수 ---

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName)
{
    // 이 함수는 VkDevice를 받지만, 이 Device가 어떤 VkInstance에 속하는지 알기 어렵습니다.
    // 가장 완벽한 방법은 vkCreateDevice를 후킹하여 <VkDevice, VkInstance> 맵을 만드는 것이지만,
    // 이는 코드를 매우 복잡하게 만듭니다.
    //
    // 대부분의 안드로이드 앱은 VkInstance를 하나만 사용하므로,
    // 첫 번째로 찾은 인스턴스의 GetDeviceProcAddr를 호출하는 방식으로 단순화합니다.
    // 이것은 100% 완벽하진 않지만, 간단한 로깅 레이어에는 충분히 안정적인 절충안입니다.
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (!g_instance_data_map.empty()) {
        LayerInstanceData* instance_data = g_instance_data_map.begin()->second.get();
        if (instance_data && instance_data->next_pfnGetDeviceProcAddr) {
            return instance_data->next_pfnGetDeviceProcAddr(device, pName);
        }
    }
    
    return nullptr;
}


VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName)
{
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)Hook_vkCreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0) return (PFN_vkVoidFunction)Hook_vkDestroyInstance;
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;

    if (instance != VK_NULL_HANDLE) {
        void* dispatch_key = *(void**)instance;
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        auto it = g_instance_data_map.find(dispatch_key);
        if (it != g_instance_data_map.end()) {
            return it->second->next_pfnGetInstanceProcAddr(instance, pName);
        }
    }

    return NULL;
}

 VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct)
{
    // [수정됨] sType 오타 수정 (NEGIATE -> NEGOTIATE)
    if (pVersionStruct == NULL || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    } else {
        ALOGE("Unsupported version: %d", pVersionStruct->loaderLayerInterfaceVersion);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

// Provide the loader layer properties 
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties)
{
    if (pProperties == NULL) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    if (*pPropertyCount < 1) {
        return VK_INCOMPLETE;
    }
    strncpy(pProperties[0].layerName, "VK_LAYER_MY_LAYER", VK_MAX_EXTENSION_NAME_SIZE);
    pProperties[0].specVersion = VK_API_VERSION_1_3;
    pProperties[0].implementationVersion = 1;
    strncpy(pProperties[0].description, "My Vulkan Layer", VK_MAX_DESCRIPTION_SIZE);
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

// Provide the loader extension properties
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    if (pLayerName == NULL || strcmp(pLayerName, "VK_LAYER_MY_LAYER") != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    // It means VK_LAYER_MY_LAYER doesn't include any extensions.
    *pPropertyCount = 0;
    return VK_SUCCESS;
}
