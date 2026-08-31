#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string source_path(const char* relative) {
    std::string path = OPENOMADA_SOURCE_DIR;
    path += "/";
    path += relative;
    return path;
}

std::string read_file(const char* relative) {
    std::ifstream input(source_path(relative));
    require(input.good(), relative);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

void test_openwrt_makefile_uses_size_oriented_cmake_package() {
    const std::string makefile = read_file("openwrt/Makefile");

    require(contains(makefile, "include $(INCLUDE_DIR)/cmake.mk"), "uses cmake.mk");
    require(contains(makefile, "+libjson-c"), "depends on json-c");
    require(contains(makefile, "+libopenssl"), "depends on openssl");
    require(contains(makefile, "+opennds"), "depends on openNDS");
    require(contains(makefile, "+conntrack"), "depends on conntrack");
    require(contains(makefile, "-DOPENOMADA_BUILD_TESTS=OFF"), "tests disabled for package");
    require(contains(makefile, "-DOPENOMADA_ENABLE_TLS=ON"), "TLS enabled for package");
    require(contains(makefile, "-DCMAKE_BUILD_TYPE=MinSizeRel"), "size build type");
    require(contains(makefile, "TARGET_CXXFLAGS += -fno-exceptions -fno-rtti"), "exception/RTTI flags");
    require(contains(makefile, "$(CP) ./src/. $(PKG_BUILD_DIR)/"), "staged source copy");
    require(contains(makefile, "$(LN) openomada-agent-native $(1)/usr/sbin/openomada-agent"), "compat symlink");
}

void test_openwrt_init_reads_uci_and_avoids_secret_env() {
    const std::string init = read_file("openwrt/files/openomada.init");

    require(contains(init, "USE_PROCD=1"), "procd enabled");
    require(contains(init, ". /lib/functions.sh"), "loads OpenWrt functions");
    require(contains(init, "config_load openomada"), "loads UCI config");
    require(contains(init, "config_get_bool enabled controller enabled 0"), "honors enabled flag");
    require(contains(init, "OPENOMADA_CONFIG="), "exports config path");
    require(contains(init, "OPENOMADA_CONTROLLER_HOST="), "exports controller host");
    require(contains(init, "OPENOMADA_OPENNDS_THEMESPEC_PATH="), "exports ThemeSpec path");
    require(contains(init, "procd_add_reload_trigger openomada"), "reload trigger");
    require(!contains(init, "OPENOMADA_PASSWORD"), "does not export password");
    require(!contains(init, "config_get password"), "does not read password into init env");
}

void test_openwrt_default_config_declares_portal_runtime() {
    const std::string config = read_file("openwrt/files/openomada.config");

    require(contains(config, "config agent 'main'"), "agent section");
    require(contains(config, "option state_path '/var/lib/openomada/managed-state.json'"), "state path");
    require(contains(config, "config portal 'main'"), "portal section");
    require(contains(config, "option engine 'opennds'"), "openNDS engine");
    require(contains(config, "option gatewayfqdn 'disable'"), "gatewayfqdn disabled");
    require(contains(config, "option flush_conntrack_on_deauth '1'"), "conntrack flush enabled");
}

void test_package_helper_scripts_are_present() {
    const std::string stage = read_file("scripts/stage_openwrt_package.sh");
    const std::string bench = read_file("scripts/benchmark_resource_usage.sh");

    require(contains(stage, "openwrt/Makefile"), "stage copies package Makefile");
    require(contains(stage, "CMakeLists.txt"), "stage copies CMake source");
    require(contains(stage, "include"), "stage copies include tree");
    require(contains(stage, "make package/openomada/compile V=s"), "stage prints SDK build command");
    require(contains(bench, "-DOPENOMADA_BUILD_TESTS=OFF"), "benchmark disables tests");
    require(contains(bench, "-fno-exceptions -fno-rtti"), "benchmark size flags");
    require(contains(bench, "strip"), "benchmark strips binary");
    require(contains(bench, "size"), "benchmark reports size");
}

} // namespace

int main() {
    test_openwrt_makefile_uses_size_oriented_cmake_package();
    test_openwrt_init_reads_uci_and_avoids_secret_env();
    test_openwrt_default_config_declares_portal_runtime();
    test_package_helper_scripts_are_present();
    std::cout << "openomada-openwrt-package-tests passed\n";
    return 0;
}
