#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>

#include <string.h>
#include <unordered_map>
#include <mutex>
#include <memory>

// Android 로깅 매크로 정의
#define LOG_TAG "MyLayer"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- 데이터 관리 및 스레드 안전성 ---

struct LayerInstanceData {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr next_pfnGetInstanceProcAddr;
    PFN_vkDestroyInstance next_pfnDestroyInstance;
};

struct LayerDeviceData {
    VkDevice device;
    PFN_vkGetDeviceProcAddr next_pfnGetDeviceProcAddr; 
};

static std::mutex g_device_mutex;
static std::unordered_map<void*, std::unique_ptr<LayerDeviceData>> g_device_data_map;

static std::mutex g_instance_mutex;
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

VKAPI_ATTR void VKAPI_CALL Hook_vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator)
{
    void* dispatch_key = *(void**)device;
    std::unique_ptr<LayerDeviceData> device_data;
    
    // 1. 디바이스 데이터 맵에서 데이터 검색 및 제거
    {
        std::lock_guard<std::mutex> lock(g_device_mutex);
        auto it = g_device_data_map.find(dispatch_key);
        if (it != g_device_data_map.end()) {
            device_data = std::move(it->second);
            g_device_data_map.erase(it);
        }
    }

    if (device_data && device_data->next_pfnGetDeviceProcAddr) {
        // 2. 다음 체인의 vkDestroyDevice를 얻어 호출
        PFN_vkDestroyDevice next_pfnDestroyDevice = (PFN_vkDestroyDevice)
            device_data->next_pfnGetDeviceProcAddr(device, "vkDestroyDevice");

        if (next_pfnDestroyDevice) {
            ALOGI("Hook_vkDestroyDevice! Device: %p", (void*)device);
            next_pfnDestroyDevice(device, pAllocator);
        } else {
            // 다음 체인의 vkDestroyDevice를 찾지 못해도 메모리 정리를 위해 경고 후 종료
            ALOGE("Hook_vkDestroyDevice: Not found next vkDestroyDevice");
        }
    } else {
        ALOGE("Hook_vkDestroyDevice: Unknown Device(%p)", (void*)device);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    // 1. 다음 레이어의 vkGetInstanceProcAddr를 찾아 다음 vkCreateDevice를 얻습니다.
    void* instance_dispatch_key = *(void**)physicalDevice;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    auto it = g_instance_data_map.find(instance_dispatch_key);

    if (it == g_instance_data_map.end()) {
        ALOGE("Hook_vkCreateDevice: Not found LayerInstanceData");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LayerInstanceData* instance_data = it->second.get();
    
    // next_pfnGetInstanceProcAddr를 통해 다음 체인의 vkCreateDevice 포인터를 얻습니다.
    PFN_vkCreateDevice next_pfnCreateDevice = (PFN_vkCreateDevice)
        instance_data->next_pfnGetInstanceProcAddr(instance_data->instance, "vkCreateDevice");

    if (!next_pfnCreateDevice) {
        ALOGE("Hook_vkCreateDevice: Not found next vkCreateDevice.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 2. Layer 체인 정보를 갱신 (vkCreateInstance와 동일한 로직을 적용해야 함)
    VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                               layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = (VkLayerDeviceCreateInfo*)layerCreateInfo->pNext;
    }
    if (!layerCreateInfo) {
        ALOGE("Hook_vkCreateDevice: Not found VK_LAYER_LINK_INFO.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 다음 Layer의 정보를 가리키도록 체인 갱신
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
    
    // 3. 다음 체인의 vkCreateDevice 호출
    VkResult result = next_pfnCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (result != VK_SUCCESS) {
        return result;
    }
    
    // 4. LayerDeviceData 초기화 및 연결
    auto device_data = std::make_unique<LayerDeviceData>();
    device_data->device = *pDevice;

    // next_pfnGetDeviceProcAddr를 얻어 저장합니다.
    PFN_vkGetDeviceProcAddr next_pfnGetDeviceProcAddr = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    device_data->next_pfnGetDeviceProcAddr = next_pfnGetDeviceProcAddr;
    
    // VkDevice 핸들에서 디스패치 키를 가져와 맵에 저장합니다.
    void* device_dispatch_key = *(void**)*pDevice;
    {
        std::lock_guard<std::mutex> lock(g_device_mutex);
        g_device_data_map[device_dispatch_key] = std::move(device_data);
    }

    ALOGI("Hook_vkCreateDevice Success, Device: %p", (void*)*pDevice);
    return VK_SUCCESS;
}


// --- 로더 인터페이스 함수 ---

// Find a function pointer of device level functions (ex. vkCmdDraw, vkQueueSubmit)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName)
{
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    
    // (선택 사항) 훅킹할 디바이스 레벨 함수 목록을 여기에 추가합니다.
    // if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)Hook_vkQueueSubmit;

    if (device != VK_NULL_HANDLE) {
        void* dispatch_key = *(void**)device;
        std::lock_guard<std::mutex> lock(g_device_mutex);
        auto it = g_device_data_map.find(dispatch_key);
        
        if (it != g_device_data_map.end()) {
            LayerDeviceData* device_data = it->second.get();
            if (device_data && device_data->next_pfnGetDeviceProcAddr) {
                return device_data->next_pfnGetDeviceProcAddr(device, pName);
            }
        }
    }
    
    return nullptr;
}

// Find a function pointer of instance level functions. (ex. vkCreateInstance, vkCreateDevice)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName)
{
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)Hook_vkCreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0) return (PFN_vkVoidFunction)Hook_vkDestroyInstance;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)Hook_vkCreateDevice;
    if (strcmp(pName, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)Hook_vkDestroyDevice;
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
