#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/persistence/session_state_repository.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::domain::MacAddress must_parse_mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC should parse");
    return *parsed;
}

std::string temp_dir() {
    char path[] = "/tmp/openomada-persistence-XXXXXX";
    char* created = ::mkdtemp(path);
    require(created != nullptr, "mkdtemp");
    return std::string(created);
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    require(input.good(), "file opens");
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

openomada::lifecycle::ManagedState managed_state() {
    openomada::lifecycle::ManagedState state;
    state.version = 1;
    state.mac = "02:11:22:33:44:55";
    state.controller_host = "controller.example.test";
    state.controller_id = "0123456789abcdef0123456789abcdef";
    state.manage_port = 29814;
    state.site_id = "0123456789abcdef01234567";
    state.username = "lab-user";
    state.config_version = 7;
    state.sequence_id = 9;
    state.updated_at = 1780000000;
    return state;
}

void test_round_trip_does_not_persist_secrets() {
    const std::string dir = temp_dir();
    const std::string path = dir + "/managed/state.json";
    openomada::persistence::JsonSessionStateRepository repository(
        path,
        must_parse_mac("02-11-22-33-44-55"),
        "controller.example.test"
    );

    auto save = repository.save(managed_state());
    require(save.ok, "state saves");

    auto loaded = repository.load();
    require(loaded.ok, "state load status");
    require(loaded.found, "state found");
    require(loaded.state.mac == "02:11:22:33:44:55", "MAC normalized");
    require(loaded.state.controller_id == "0123456789abcdef0123456789abcdef", "controller id");
    require(loaded.state.manage_port == 29814, "manage port");
    require(loaded.state.site_id == "0123456789abcdef01234567", "site id");
    require(loaded.state.username == "lab-user", "username");
    require(loaded.state.config_version.value_or(0) == 7, "config version");
    require(loaded.state.sequence_id.value_or(0) == 9, "sequence id");

    const std::string raw = read_file(path);
    require(raw.find("password") == std::string::npos, "password not persisted");
    require(raw.find("device_password") == std::string::npos, "device_password not persisted");

    struct stat st {};
    require(::stat(path.c_str(), &st) == 0, "stat saved state");
    require((st.st_mode & 0777) == 0600, "state file permissions are owner-only");

    require(repository.clear(), "state clears");
    require(!repository.load().found, "cleared state not found");
    require(repository.clear(), "second clear is idempotent");
    (void)::rmdir((dir + "/managed").c_str());
    (void)::rmdir(dir.c_str());
}

void test_identity_mismatch_is_ignored_and_save_is_rejected() {
    const std::string dir = temp_dir();
    const std::string path = dir + "/state.json";
    openomada::persistence::JsonSessionStateRepository repository(
        path,
        must_parse_mac("02-11-22-33-44-55"),
        "controller.example.test"
    );

    auto foreign = managed_state();
    foreign.mac = "02:11:22:33:44:66";
    require(!repository.save(foreign).ok, "foreign save rejected");

    std::ofstream output(path);
    output << R"({"version":1,"mac":"02:11:22:33:44:66","controller_host":"controller.example.test","controller_id":"controller","manage_port":29814})";
    output.close();

    auto loaded = repository.load();
    require(loaded.ok, "foreign state ignored without hard load failure");
    require(!loaded.found, "foreign state not returned");
    require(loaded.error.find("another device MAC") != std::string::npos, "foreign state reason");

    (void)::unlink(path.c_str());
    (void)::rmdir(dir.c_str());
}

void test_invalid_state_is_ignored() {
    const std::string dir = temp_dir();
    const std::string path = dir + "/state.json";
    openomada::persistence::JsonSessionStateRepository repository(
        path,
        must_parse_mac("02-11-22-33-44-55"),
        "controller.example.test"
    );

    std::ofstream output(path);
    output << R"({"version":2,"mac":"02:11:22:33:44:55","controller_host":"controller.example.test","controller_id":"controller","manage_port":29814})";
    output.close();

    auto loaded = repository.load();
    require(loaded.ok, "unsupported version ignored without hard load failure");
    require(!loaded.found, "unsupported version not returned");

    (void)::unlink(path.c_str());
    (void)::rmdir(dir.c_str());
}

} // namespace

int main() {
    test_round_trip_does_not_persist_secrets();
    test_identity_mismatch_is_ignored_and_save_is_rejected();
    test_invalid_state_is_ignored();
    std::cout << "openomada-persistence-tests passed\n";
    return 0;
}
