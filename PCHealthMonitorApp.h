#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "SystemMetrics.h"
#include "TerminalUI.h"

namespace pcm {

class PCHealthMonitorApp {
public:
    int run();

private:
    static void signalHandler(int);
    void setupSignals() const;
    void inputLoop();
    std::vector<std::string> buildFrame();

    static PCHealthMonitorApp* instance_;

    std::atomic<bool> running_{true};
    std::atomic<int> scrollOffset_{0};
    std::atomic<int> totalLines_{0};

    CPUMonitor cpuMonitor_;
    CPUInfo cpuInfo_;
    OSInfo osInfo_;
    Viewport viewport_;
};

}  // namespace pcm
