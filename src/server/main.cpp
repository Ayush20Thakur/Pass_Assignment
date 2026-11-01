#include <iostream>
#include <string>
#include "../core/version.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

int main(int argc, char* argv[]) {
    int port = 0; // 0 selects an ephemeral free port if bound
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }

    httplib::Server server;

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"status", "ok"},
            {"version", std::string(forge::core::get_version_string())}
        };
        res.set_content(body.dump(), "application/json");
    });

    if (port > 0) {
        std::cout << "Forge Server running on 127.0.0.1:" << port << "\n";
        server.listen("127.0.0.1", port);
    } else {
        std::cout << "Forge Server initialized (version " << forge::core::get_version_string() << ")\n";
    }

    return 0;
}
