/*
 * Template: cmake-sdk main
 * Demonstrates querying board capabilities at runtime via /proc/device-tree
 * and /sys, so the SAME binary runs on multiple RK SoCs without rebuild.
 * See boards/probe.sh for the shell equivalent.
 */
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static std::string read_file(const char *path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

static std::string detect_soc() {
    std::string compat = read_file("/proc/device-tree/compatible");
    /* e.g. "rockchip,rk3588\0rockchip,rk35xx" */
    auto pos = compat.find("rockchip,rk");
    if (pos != std::string::npos) {
        return compat.substr(pos + 11, 6);  /* "rk3588" */
    }
    return "unknown";
}

int main() {
    std::string soc = detect_soc();
    printf("SoC: %s\n", soc.c_str());

    /* NPU cores: /sys/kernel/debug/rknpu/version or /proc/rknpu */
    std::string npu_ver = read_file("/sys/kernel/debug/rknpu/version");
    if (!npu_ver.empty()) printf("NPU version: %s\n", npu_ver.c_str());

    /* TODO: dispatch to SoC-specific code path based on soc.
     * See boards/<soc>.md for capability matrix. */
    return 0;
}