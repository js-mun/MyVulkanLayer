cmake_minimum_required(VERSION 3.19)
project(MyLayer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# External
set(VULKAN_HEADERS_DIR external/Vulkan-Headers/include)

# Android 
find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

add_library(MyLayer SHARED
    src/my_layer.cpp
    src/utils.cpp
)

target_include_directories(MyLayer PRIVATE 
    ${VULKAN_HEADERS_DIR}
    src
)
target_link_libraries(MyLayer
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)
