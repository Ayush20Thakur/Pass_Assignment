#include <iostream>
#include <string>

#include "server.hpp"

int main(int argc, char* argv[]) {
    int port = 0; // 0 chooses a random ephemeral free port on 127.0.0.1
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }

    forge::server::ForgeServer server(port);
    if (!server.start(false)) { // Blocking mode for standalone server process
        std::cerr << "Failed to bind Forge Server on 127.0.0.1:" << port << "\n";
        return 1;
    }

    std::cout << "Forge Server running on 127.0.0.1:" << server.get_port() << "\n";

    // Write ephemeral port to server.port for discovery
    {
        std::ofstream port_file("server.port", std::ios::trunc);
        if (port_file.is_open()) {
            port_file << server.get_port() << "\n";
        }
    }

    server.wait();

    // Clean up port file on exit
    std::remove("server.port");
    return 0;
}
