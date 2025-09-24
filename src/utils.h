#pragma once

#include <android/log.h>
#include <iostream>

#define LOG_TAG "MyLayer"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

std::string get_app_package_name();
bool should_enable_layer();