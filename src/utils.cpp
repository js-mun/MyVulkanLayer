#include "utils.h"

#include <fstream>
#include <string>
#include <cstdlib>

static std::string cur_pkg = get_app_package_name();

std::string get_app_package_name() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkg;
    if (cmdline) std::getline(cmdline, pkg, '\0');
    return pkg;
}

bool should_enable_layer() {
    // adb shell export MY_VK_LAYER_PACKAGE=com.example.myapp
    const char* target_pkg = std::getenv("MY_LAYER_PACKAGE");
    if (!target_pkg) return false;

    return cur_pkg == target_pkg;
}