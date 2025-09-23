cmake_minimum_required(VERSION 3.19)
project(MyLayer CXX)

set(CMAKE_CXX_STANDARD 20)

# Android 
find_library(log-lib log)
find_library(android-lib android)

# Vulkan
find_library(vulkan-lib vulkan)

add_library(MyLayer SHARED
    src/overlay_layer.cpp
)

target_link_libraries(MyLayer
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)
