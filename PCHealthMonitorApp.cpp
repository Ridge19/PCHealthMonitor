#include "PCHealthMonitorApp.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace pcm {

PCHealthMonitorApp* PCHealthMonitorApp::instance_ = nullptr;

namespace {

constexpr auto kInputPollInterval = std::chrono::milliseconds(16);
constexpr auto kRenderInterval = std::chrono::milliseconds(16);
constexpr int kEscapeSeqMaxBytes = 4;
constexpr int kEscapeSeqTotalBudgetMs = 12;

std::string platformLabel() {
#ifdef _WIN32
    return "Windows";
#else
    return "Linux";
#endif
}

std::string makeBar(double percent, int width = 30) {
    int filled = static_cast<int>(std::round(percent / 100.0 * width));
    filled = std::clamp(filled, 0, width);

    std::string color;
    if (percent < 50.0) {
        color = Color::GREEN;
    } else if (percent < 80.0) {
        color = Color::YELLOW;
    } else {
        color = Color::RED;
    }

    std::string bar = color + "[";
    for (int i = 0; i < width; ++i) {
        bar += (i < filled) ? "\u2588" : "\u2591";
    }
    bar += "]" + Color::RESET;
    return bar;
}

std::string fmtPercent(double val, int width = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << std::setw(width) << std::right << val << "%";
    return out.str();
}

std::string fmtGB(double val, int precision = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << val << " GB";
    return out.str();
}

std::string fmtMHz(double val) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << std::setw(5) << std::right << val << " MHz";
    return out.str();
}

std::string fmtTemp(double val) {
    std::string color;
    if (val < 50.0) {
        color = Color::GREEN;
    } else if (val < 75.0) {
        color = Color::YELLOW;
    } else {
        color = Color::RED;
    }

    std::ostringstream out;
    out << color << std::fixed << std::setprecision(1) << std::setw(5) << std::right << val << " \u00b0C" << Color::RESET;
    return out.str();
}

}  // namespace

void PCHealthMonitorApp::signalHandler(int) {
    if (instance_ != nullptr) {
        instance_->running_ = false;
    }
}

void PCHealthMonitorApp::setupSignals() const {
#ifdef _WIN32
    std::signal(SIGINT, PCHealthMonitorApp::signalHandler);
    std::signal(SIGTERM, PCHealthMonitorApp::signalHandler);
#else
    struct sigaction sa {};
    sa.sa_handler = PCHealthMonitorApp::signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

std::vector<std::string> PCHealthMonitorApp::buildFrame() {
    std::vector<std::string> lines;
    const TermSize ts = getTermSize();

    auto S = [&](const std::string& s) { lines.push_back(s); };

    S("");
    S(Color::BOLD + Color::MAGENTA +
      "   \u2554\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2557" +
      Color::RESET);
    S(Color::BOLD + Color::MAGENTA +
            "   \u2551                     PC HEALTH MONITOR (" + platformLabel() +")                      \u2551" + Color::RESET);
    S(Color::BOLD + Color::MAGENTA +
      "   \u255a\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"
      "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u255d" +
      Color::RESET);

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 SYSTEM \u2550\u2550\u2550" + Color::RESET);
    const LoadAvg la = SystemMetrics::getLoadAverage();
    S("  " + Color::WHITE + "Host:     " + Color::RESET + osInfo_.hostname + Color::DIM + "  |  " + Color::RESET +
      osInfo_.prettyName);
    S("  " + Color::WHITE + "Kernel:   " + Color::RESET + osInfo_.kernel + Color::DIM + "  |  Uptime: " + Color::RESET +
      SystemMetrics::getUptime());
    {
        std::ostringstream out;
        out << "  " << Color::WHITE << "Load:     " << Color::RESET << std::fixed << std::setprecision(2) << la.avg1 << "  "
            << la.avg5 << "  " << la.avg15 << Color::DIM << " (1m 5m 15m)" << Color::RESET
            << "  |  Procs: " << SystemMetrics::getProcessCount();
        S(out.str());
    }
    S("");

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 CPU \u2550\u2550\u2550" + Color::RESET);
    S("  " + Color::WHITE + "Model:    " + Color::RESET + cpuInfo_.modelName);
    {
        const double curMHz = SystemMetrics::getCurrentAvgMHz(cpuInfo_.logicalCores);
        std::ostringstream out;
        out << "  " << Color::WHITE << "Cores:    " << Color::RESET << cpuInfo_.physicalCores << "P / " << cpuInfo_.logicalCores
            << "L"
            << "  |  " << Color::WHITE << "Freq: " << Color::RESET << fmtMHz(curMHz);
        if (cpuInfo_.maxMHz > 0) {
            out << " / " << fmtMHz(cpuInfo_.maxMHz);
        }
        S(out.str());
    }
    if (!cpuInfo_.cacheSize.empty()) {
        S("  " + Color::WHITE + "Cache:    " + Color::RESET + cpuInfo_.cacheSize);
    }

    std::vector<double> coreUsages;
    const double cpuTotal = cpuMonitor_.getUsage(coreUsages);
    S("");
    if (cpuTotal >= 0) {
        S("  " + Color::BOLD + "Total:    " + Color::RESET + makeBar(cpuTotal, 28) + " " + fmtPercent(cpuTotal));
    }

    if (!coreUsages.empty()) {
        const int cols = (ts.cols >= 100) ? 2 : 1;
        for (size_t i = 0; i < coreUsages.size(); i += cols) {
            std::ostringstream row;
            for (int c = 0; c < cols && i + static_cast<size_t>(c) < coreUsages.size(); ++c) {
                const int idx = static_cast<int>(i + static_cast<size_t>(c));
                row << "  Core " << std::setw(2) << idx << ": " << makeBar(coreUsages[idx], 14) << " "
                    << fmtPercent(coreUsages[idx]);
                if (c == 0 && cols > 1) {
                    row << "   ";
                }
            }
            S(row.str());
        }
    }
    S("");

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 MEMORY \u2550\u2550\u2550" + Color::RESET);
    const MemoryInfo mem = SystemMetrics::getMemoryInfo();
    S("  " + Color::WHITE + "RAM:      " + Color::RESET + makeBar(mem.usagePercent, 28) + " " + fmtPercent(mem.usagePercent) +
      Color::DIM + "  (" + fmtGB(mem.usedGB) + " / " + fmtGB(mem.totalGB) + ")" + Color::RESET);
    S("  " + Color::DIM + "          Avail: " + fmtGB(mem.availableGB) + "  |  Buff/Cache: " + fmtGB(mem.buffCacheGB) +
      Color::RESET);
    if (mem.swapTotalGB > 0.01) {
        const double swapPercent = mem.swapTotalGB > 0 ? (mem.swapUsedGB / mem.swapTotalGB) * 100.0 : 0.0;
        S("  " + Color::WHITE + "Swap:     " + Color::RESET + makeBar(swapPercent, 28) + " " + fmtPercent(swapPercent) +
          Color::DIM + "  (" + fmtGB(mem.swapUsedGB) + " / " + fmtGB(mem.swapTotalGB) + ")" + Color::RESET);
    }
    S("");

    const auto gpuInfos = SystemMetrics::getGPUInfo();
    if (!gpuInfos.empty()) {
        S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 GPU \u2550\u2550\u2550" + Color::RESET);
        for (const auto& gpu : gpuInfos) {
            std::ostringstream out;
            out << "  " << Color::WHITE << gpu.name << Color::RESET;
            if (!gpu.vram.empty()) {
                out << "  |  VRAM: " << gpu.vram;
            }
            if (!gpu.temperature.empty()) {
                out << "  |  Temp: " << gpu.temperature;
            }
            if (!gpu.utilization.empty()) {
                out << "  |  Load: " << gpu.utilization;
            }
            S(out.str());
        }
        S("");
    }

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 TEMPERATURE \u2550\u2550\u2550" + Color::RESET);
    const auto temps = SystemMetrics::getTemperatures();
    if (temps.empty()) {
#ifdef _WIN32
        S("  " + Color::YELLOW + "\u26a0 No temperature sensors exposed by this Windows setup" + Color::RESET);
#else
        S("  " + Color::YELLOW + "\u26a0 No sensors (try: sudo apt install lm-sensors && sudo sensors-detect)" + Color::RESET);
#endif
    } else {
        const int cols = (ts.cols >= 100) ? 3 : 2;
        for (size_t i = 0; i < temps.size(); i += cols) {
            std::ostringstream row;
            for (int c = 0; c < cols && i + static_cast<size_t>(c) < temps.size(); ++c) {
                const auto& t = temps[i + static_cast<size_t>(c)];
                std::string sensor = t.name;
                if (sensor.length() > 18) {
                    sensor = sensor.substr(0, 15) + "...";
                }
                row << "  " << Color::WHITE << std::setw(18) << std::left << sensor << Color::RESET << fmtTemp(t.celsius);
                if (c < cols - 1) {
                    row << "  ";
                }
            }
            S(row.str());
        }
    }
    S("");

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 DISKS \u2550\u2550\u2550" + Color::RESET);
    for (const auto& disk : SystemMetrics::getDiskInfo()) {
        std::string mountPoint = disk.mountPoint;
        if (mountPoint.length() > 15) {
            mountPoint = "..." + mountPoint.substr(mountPoint.length() - 12);
        }
        std::ostringstream out;
        out << "  " << Color::BOLD << std::setw(15) << std::left << mountPoint << Color::RESET << " "
            << makeBar(disk.usagePercent, 20) << " " << fmtPercent(disk.usagePercent) << Color::DIM << "  " << std::fixed
            << std::setprecision(1) << disk.usedGB << "/" << disk.totalGB << "GB"
            << "  [" << disk.filesystem << "]" << Color::RESET;
        S(out.str());
    }
    S("");

    const auto networks = SystemMetrics::getNetworkInfo();
    if (!networks.empty()) {
        S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 NETWORK \u2550\u2550\u2550" + Color::RESET);
        for (const auto& n : networks) {
            const std::string stateColor = (n.state == "up") ? Color::GREEN : Color::RED;
            std::ostringstream out;
            out << "  " << Color::BOLD << std::setw(10) << std::left << n.name << Color::RESET << stateColor << " [" << n.state
                << "]" << Color::RESET << "  Spd: " << std::setw(10) << n.speed << "  MAC: " << Color::DIM << n.macAddress
                << Color::RESET;
            S(out.str());
            if (!n.rxBytes.empty()) {
                S("  " + std::string(10, ' ') + Color::DIM + " RX: " + n.rxBytes + "  TX: " + n.txBytes + Color::RESET);
            }
        }
        S("");
    }

    const BatteryInfo battery = SystemMetrics::getBatteryInfo();
    if (battery.hasBattery) {
        S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 BATTERY \u2550\u2550\u2550" + Color::RESET);
        std::string batteryColor;
        if (battery.percent > 60) {
            batteryColor = Color::GREEN;
        } else if (battery.percent > 20) {
            batteryColor = Color::YELLOW;
        } else {
            batteryColor = Color::RED;
        }
        S("  " + batteryColor + makeBar(battery.percent, 28) + " " + std::to_string(battery.percent) + "% - " + battery.status +
          Color::RESET);
        {
            std::ostringstream out;
            out << "  " << Color::DIM << " Cycles: " << Color::RESET;
            if (battery.cycleCount >= 0) {
                out << battery.cycleCount;
            } else {
                out << "N/A";
            }
            if (!battery.health.empty()) {
                out << Color::DIM << "  |  Health: " << Color::RESET << battery.health;
            }
            S(out.str());
        }
        if (battery.designCapacityWh > 0.0 && battery.fullCapacityWh > 0.0) {
            std::ostringstream out;
            out << "  " << Color::DIM << " Full Cap: " << Color::RESET << std::fixed << std::setprecision(2)
                << battery.fullCapacityWh << " Wh"
                << Color::DIM << " / Design: " << Color::RESET << std::fixed << std::setprecision(2)
                << battery.designCapacityWh << " Wh"
                << Color::DIM << "  (" << Color::RESET << fmtPercent(battery.fullCapacityPct, 5) << Color::DIM
                << " of design)" << Color::RESET;
            S(out.str());
        }
        S("");
    }

    S(Color::BOLD + Color::CYAN + "  \u2550\u2550\u2550 TOP PROCESSES \u2550\u2550\u2550" + Color::RESET);
    {
        std::ostringstream out;
        out << "  " << Color::DIM << std::setw(7) << std::right << "PID" << std::setw(8) << "CPU%" << std::setw(8) << "MEM%"
            << "  " << std::left << "COMMAND" << Color::RESET;
        S(out.str());
    }
    for (const auto& process : SystemMetrics::getTopProcesses(5)) {
        std::string cpuColor;
        if (process.cpuPct < 25.0) {
            cpuColor = Color::GREEN;
        } else if (process.cpuPct < 75.0) {
            cpuColor = Color::YELLOW;
        } else {
            cpuColor = Color::RED;
        }
        std::ostringstream out;
        out << "  " << std::setw(7) << std::right << process.pid << cpuColor << std::setw(7) << std::fixed
            << std::setprecision(1) << process.cpuPct << "%" << Color::RESET << std::setw(7) << std::fixed
            << std::setprecision(1) << process.memPct << "%"
            << "  " << std::left << process.name;
        S(out.str());
    }
    S("");

    {
        const auto now = std::chrono::system_clock::now();
        const auto raw = std::chrono::system_clock::to_time_t(now);
        struct tm local {};
    #ifdef _WIN32
        localtime_s(&local, &raw);
    #else
        localtime_r(&raw, &local);
    #endif

        std::ostringstream out;
        out << Color::DIM << "  Updated: " << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "  |  Refresh: 2s" << Color::RESET;
        S(out.str());
    }
    S("");

    return lines;
}

void PCHealthMonitorApp::inputLoop() {
#ifdef _WIN32
    while (running_) {
        if (!_kbhit()) {
            std::this_thread::sleep_for(kInputPollInterval);
            continue;
        }

        int key = _getch();

        const TermSize ts = getTermSize();
        const int viewHeight = ts.rows - 2;
        const int total = totalLines_.load();
        const int maxScroll = std::max(0, total - viewHeight);
        const int current = scrollOffset_.load();

        if (key == 'q' || key == 'Q') {
            running_ = false;
        } else if (key == 'k' || key == 'K') {
            scrollOffset_ = std::max(0, current - 1);
        } else if (key == 'j' || key == 'J') {
            scrollOffset_ = std::min(maxScroll, current + 1);
        } else if (key == 'g') {
            scrollOffset_ = 0;
        } else if (key == 'G') {
            scrollOffset_ = maxScroll;
        } else if (key == ' ') {
            scrollOffset_ = std::min(maxScroll, current + viewHeight);
        } else if (key == 0 || key == 224) {
            const int ext = _getch();
            switch (ext) {
                case 72:  // up
                    scrollOffset_ = std::max(0, current - 1);
                    break;
                case 80:  // down
                    scrollOffset_ = std::min(maxScroll, current + 1);
                    break;
                case 73:  // page up
                    scrollOffset_ = std::max(0, current - viewHeight);
                    break;
                case 81:  // page down
                    scrollOffset_ = std::min(maxScroll, current + viewHeight);
                    break;
                case 71:  // home
                    scrollOffset_ = 0;
                    break;
                case 79:  // end
                    scrollOffset_ = maxScroll;
                    break;
                default:
                    break;
            }
        }
    }
#else
    while (running_) {
        struct pollfd pfd {};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        const int ret = poll(&pfd, 1, static_cast<int>(kInputPollInterval.count()));
        if (ret <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        char c = 0;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            continue;
        }

        const TermSize ts = getTermSize();
        const int viewHeight = ts.rows - 2;
        const int total = totalLines_.load();
        const int maxScroll = std::max(0, total - viewHeight);
        const int current = scrollOffset_.load();

        if (c == 'q' || c == 'Q') {
            running_ = false;
        } else if (c == '\033') {
            char seq[kEscapeSeqMaxBytes + 1] = {};
            int seqLen = 0;
            struct pollfd pf2 {};
            pf2.fd = STDIN_FILENO;
            pf2.events = POLLIN;

            // Collect the remaining ANSI escape sequence bytes quickly with a small total time budget.
            int remainingBudget = kEscapeSeqTotalBudgetMs;
            while (seqLen < kEscapeSeqMaxBytes && remainingBudget > 0) {
                const int step = std::min(remainingBudget, 2);
                const int ready = poll(&pf2, 1, step);
                remainingBudget -= step;
                if (ready <= 0 || !(pf2.revents & POLLIN)) {
                    continue;
                }

                const ssize_t n = read(STDIN_FILENO, &seq[seqLen], 1);
                if (n == 1) {
                    ++seqLen;
                } else {
                    break;
                }
            }

            const std::string escSeq(seq, seq + seqLen);

            if (escSeq == "[A") {
                scrollOffset_ = std::max(0, current - 1);
            } else if (escSeq == "[B") {
                scrollOffset_ = std::min(maxScroll, current + 1);
            } else if (escSeq == "[5~") {
                scrollOffset_ = std::max(0, current - viewHeight);
            } else if (escSeq == "[6~") {
                scrollOffset_ = std::min(maxScroll, current + viewHeight);
            } else if (escSeq == "[H" || escSeq == "[1~" || escSeq == "OH") {
                scrollOffset_ = 0;
            } else if (escSeq == "[F" || escSeq == "[4~" || escSeq == "OF") {
                scrollOffset_ = maxScroll;
            }
        } else if (c == 'k' || c == 'K') {
            scrollOffset_ = std::max(0, current - 1);
        } else if (c == 'j' || c == 'J') {
            scrollOffset_ = std::min(maxScroll, current + 1);
        } else if (c == 'g') {
            scrollOffset_ = 0;
        } else if (c == 'G') {
            scrollOffset_ = maxScroll;
        } else if (c == ' ') {
            scrollOffset_ = std::min(maxScroll, current + viewHeight);
        }
    }
#endif
}

int PCHealthMonitorApp::run() {
    instance_ = this;
    setupSignals();

    TerminalSession terminal;

    cpuInfo_ = SystemMetrics::getCPUInfo();
    osInfo_ = SystemMetrics::getOSInfo();

    std::vector<double> dummy;
    cpuMonitor_.getUsage(dummy);

    std::thread inputThread(&PCHealthMonitorApp::inputLoop, this);

    auto lines = buildFrame();
    totalLines_ = static_cast<int>(lines.size());

    const auto refreshInterval = std::chrono::seconds(2);
    const auto renderInterval = kRenderInterval;
    auto nextRefresh = std::chrono::steady_clock::now() + refreshInterval;
    std::future<std::vector<std::string>> pendingRefresh;
    bool refreshInFlight = false;

    int lastRenderedScroll = -1;

    while (running_) {
        bool frameRebuilt = false;
        const auto now = std::chrono::steady_clock::now();

        if (!refreshInFlight && now >= nextRefresh) {
            pendingRefresh = std::async(std::launch::async, [this]() {
                return this->buildFrame();
            });
            refreshInFlight = true;
            nextRefresh = now + refreshInterval;
        }

        if (refreshInFlight && pendingRefresh.valid() &&
            pendingRefresh.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            lines = pendingRefresh.get();
            totalLines_ = static_cast<int>(lines.size());
            frameRebuilt = true;
            refreshInFlight = false;
        }

        const TermSize ts = getTermSize();
        const int viewHeight = ts.rows - 2;
        const int maxScroll = std::max(0, static_cast<int>(lines.size()) - viewHeight);

        int scroll = scrollOffset_.load();
        if (scroll > maxScroll) {
            scroll = maxScroll;
            scrollOffset_ = scroll;
        }

        const bool scrollChanged = (scroll != lastRenderedScroll);
        if (frameRebuilt || scrollChanged || lastRenderedScroll < 0) {
            if (frameRebuilt || lastRenderedScroll < 0) {
                viewport_.setFrame(lines);
            }
            viewport_.render(scroll);
            lastRenderedScroll = scroll;
        }

        std::this_thread::sleep_for(renderInterval);
    }

    if (refreshInFlight && pendingRefresh.valid()) {
        (void)pendingRefresh.get();
    }

    if (inputThread.joinable()) {
        inputThread.join();
    }

    instance_ = nullptr;
    std::cout << "\nPC Health Monitor stopped.\n";
    return 0;
}

}  // namespace pcm
