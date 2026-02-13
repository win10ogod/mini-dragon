#include "app.hpp"

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    minidragon::App app;
    return app.run();
}
#else
int main() {
    minidragon::App app;
    return app.run();
}
#endif
