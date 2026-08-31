#include <iostream>

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << "openomada-agent-native 0.1.0\n";
        return 0;
    }

    std::cout << "openomada-agent-native: C++ runtime skeleton\n";
    return 0;
}

