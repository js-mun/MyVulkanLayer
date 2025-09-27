adb shell mkdir /data/local/tmp/vulkan_layers/
adb push build/android/libMyLayer.so /data/local/tmp/vulkan_layers/
adb push my_layer.json /data/local/tmp/vulkan_layers/

# Apply for per-app
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app com.HoYoverse.hkrpgoversea
adb shell settings put global gpu_debug_layers VK_LAYER_MY_LAYER
adb shell settings put global gpu_debug_layer_path /data/local/tmp/vulkan_layers

# Apply for all packages
# adb shell setprop debug.vulkan.layers VK_LAYER_MY_LAYER
