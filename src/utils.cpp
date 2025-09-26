#include "utils.h"

#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/system_properties.h>

static std::string cur_pkg = get_app_package_name();

std::string get_app_package_name() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkg;
    if (cmdline) std::getline(cmdline, pkg, '\0');
    return pkg;
}

bool should_enable_layer() {
    // adb shell setprop debug.my_layer_package com.example.myapp
    char value[PROP_VALUE_MAX];
    __system_property_get("debug.my_layer_package", value);

    std::string target_pkg(value);
    if (target_pkg.empty()) return false;

    return cur_pkg == target_pkg;
}