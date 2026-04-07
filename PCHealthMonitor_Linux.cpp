#include "PCHealthMonitorApp.h"

#ifdef _WIN32
#include <clocale>
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    // Ensure Unicode UI glyphs (box drawing, bars, arrows) render correctly.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
#endif

    pcm::PCHealthMonitorApp app;
    return app.run();
}
