adb push build/android/libMyLayer.so /data/local/tmp/vulkan_layers/
adb push my_layer.json /data/local/tmp/vulkan_layers/

adb shell setprop debug.vulkan.layers VK_LAYER_MY_LAYER
