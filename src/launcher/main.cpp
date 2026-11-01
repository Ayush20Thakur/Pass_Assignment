#include <iostream>
#include "../core/version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Phase 0 placeholder: verify core link and version string
    const std::string_view version = forge::core::get_version_string();
    (void)version;
    return 0;
}
#else
int main() {
    std::cout << "Forge Launcher " << forge::core::get_version_string() << "\n";
    return 0;
}
#endif
