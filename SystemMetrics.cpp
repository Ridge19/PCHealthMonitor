#include "SystemMetrics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pcm {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
        content.pop_back();
    }
    return content;
}

std::string execCommand(const std::string& cmd) {
    std::array<char, 256> buffer{};
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    pclose(pipe);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

std::string formatBytes(long long bytes) {
    std::ostringstream out;
    if (bytes >= 1000000000LL) {
        out << std::fixed << std::setprecision(2) << bytes / 1e9 << " GB";
    } else if (bytes >= 1000000LL) {
        out << std::fixed << std::setprecision(1) << bytes / 1e6 << " MB";
    } else if (bytes >= 1000LL) {
        out << std::fixed << std::setprecision(0) << bytes / 1e3 << " KB";
    } else {
        out << bytes << " B";
    }
    return out.str();
}

}  // namespace

CPUMonitor::CPUMonitor() {
    numCores_ = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    const auto times = readProcStat();
    if (!times.empty()) {
        prevTotal_ = times[0];
        prevTimes_.assign(times.begin() + 1, times.end());
    }
}

int CPUMonitor::getNumCores() const {
    return numCores_;
}

std::vector<CPUTimes> CPUMonitor::readProcStat() const {
    std::vector<CPUTimes> times;
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return times;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 3) != "cpu") {
            continue;
        }

        CPUTimes t{};
        std::string label;
        std::istringstream iss(line);
        iss >> label >> t.user >> t.nice >> t.system >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;
        times.push_back(t);
    }
    return times;
}

double CPUMonitor::getUsage(std::vector<double>& perCore) {
    const auto times = readProcStat();
    perCore.clear();
    if (times.empty()) {
        return -1.0;
    }

    double totalUsage = -1.0;
    if (!firstRead_) {
        const auto& cur = times[0];
        const auto& prev = prevTotal_;
        const long long td = cur.total() - prev.total();
        const long long id = cur.totalIdle() - prev.totalIdle();

        if (td > 0) {
            totalUsage = (1.0 - static_cast<double>(id) / static_cast<double>(td)) * 100.0;
        }

        for (size_t i = 1; i < times.size() && i - 1 < prevTimes_.size(); ++i) {
            const auto& c = times[i];
            const auto& p = prevTimes_[i - 1];
            const long long ttd = c.total() - p.total();
            const long long iid = c.totalIdle() - p.totalIdle();
            perCore.push_back((ttd > 0) ? (1.0 - static_cast<double>(iid) / static_cast<double>(ttd)) * 100.0 : 0.0);
        }
    }

    prevTotal_ = times[0];
    prevTimes_.clear();
    for (size_t i = 1; i < times.size(); ++i) {
        prevTimes_.push_back(times[i]);
    }
    firstRead_ = false;

    return totalUsage;
}

CPUInfo SystemMetrics::getCPUInfo() {
    CPUInfo info;
    info.logicalCores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));

    std::ifstream file("/proc/cpuinfo");
    if (!file.is_open()) {
        return info;
    }

    std::string line;
    std::vector<int> coreIds;
    while (std::getline(file, line)) {
        const auto cp = line.find(':');
        if (cp == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, cp);
        std::string val = line.substr(cp + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
                s.erase(s.begin());
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
                s.pop_back();
            }
        };

        trim(key);
        trim(val);

        if (key == "model name" && info.modelName.empty()) {
            info.modelName = val;
        } else if (key == "cache size" && info.cacheSize.empty()) {
            info.cacheSize = val;
        } else if (key == "core id") {
            try {
                const int id = std::stoi(val);
                if (std::find(coreIds.begin(), coreIds.end(), id) == coreIds.end()) {
                    coreIds.push_back(id);
                }
            } catch (...) {
            }
        }
    }

    info.physicalCores = coreIds.empty() ? info.logicalCores : static_cast<int>(coreIds.size());

    const std::string maxFreq = readFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (!maxFreq.empty()) {
        try {
            info.maxMHz = std::stod(maxFreq) / 1000.0;
        } catch (...) {
        }
    }

    struct utsname uts {};
    if (uname(&uts) == 0) {
        info.architecture = uts.machine;
    }

    return info;
}

double SystemMetrics::getCurrentAvgMHz(int logicalCores) {
    double total = 0.0;
    int count = 0;
    for (int i = 0; i < logicalCores; ++i) {
        const std::string value = readFile("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq");
        if (!value.empty()) {
            try {
                total += std::stod(value) / 1000.0;
                ++count;
            } catch (...) {
            }
        }
    }
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

MemoryInfo SystemMetrics::getMemoryInfo() {
    MemoryInfo info;
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        return info;
    }

    std::map<std::string, long long> values;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        long long val = 0;
        std::string unit;
        iss >> key >> val >> unit;
        if (!key.empty() && key.back() == ':') {
            key.pop_back();
        }
        values[key] = val;
    }

    auto toGB = [](long long kb) { return kb / (1024.0 * 1024.0); };

    info.totalGB = toGB(values["MemTotal"]);
    info.availableGB = toGB(values["MemAvailable"]);
    info.buffCacheGB = toGB(values["Buffers"] + values["Cached"]);
    info.usedGB = info.totalGB - info.availableGB;
    info.swapTotalGB = toGB(values["SwapTotal"]);
    info.swapUsedGB = toGB(values["SwapTotal"] - values["SwapFree"]);

    if (info.totalGB > 0.0) {
        info.usagePercent = (info.usedGB / info.totalGB) * 100.0;
    }

    return info;
}

std::vector<DiskInfo> SystemMetrics::getDiskInfo() {
    std::vector<DiskInfo> disks;
    std::ifstream file("/proc/mounts");
    if (!file.is_open()) {
        return disks;
    }

    std::string line;
    std::vector<std::string> seen;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string dev;
        std::string mnt;
        std::string fsType;
        iss >> dev >> mnt >> fsType;

        if (dev.find("/dev/") != 0) {
            continue;
        }
        if (fsType == "squashfs" || fsType == "tmpfs" || fsType == "devtmpfs") {
            continue;
        }
        if (std::find(seen.begin(), seen.end(), dev) != seen.end()) {
            continue;
        }
        seen.push_back(dev);

        struct statvfs st {};
        if (statvfs(mnt.c_str(), &st) != 0) {
            continue;
        }

        DiskInfo d;
        d.device = dev;
        d.mountPoint = mnt;
        d.filesystem = fsType;
        d.totalGB = static_cast<double>(st.f_blocks) * st.f_frsize / (1024.0 * 1024.0 * 1024.0);
        d.freeGB = static_cast<double>(st.f_bavail) * st.f_frsize / (1024.0 * 1024.0 * 1024.0);
        d.usedGB = d.totalGB - d.freeGB;
        if (d.totalGB < 0.01) {
            continue;
        }
        d.usagePercent = d.totalGB > 0 ? (d.usedGB / d.totalGB) * 100.0 : 0.0;
        disks.push_back(d);
    }
    return disks;
}

std::vector<TempReading> SystemMetrics::getTemperatures() {
    std::vector<TempReading> temps;

    for (int i = 0; i < 20; ++i) {
        const std::string base = "/sys/class/thermal/thermal_zone" + std::to_string(i);
        if (!fs::exists(base)) {
            break;
        }

        const std::string type = readFile(base + "/type");
        const std::string value = readFile(base + "/temp");
        if (!value.empty()) {
            try {
                const double c = std::stod(value) / 1000.0;
                if (c > 0 && c < 150) {
                    temps.push_back({type.empty() ? ("Zone " + std::to_string(i)) : type, c});
                }
            } catch (...) {
            }
        }
    }

    try {
        for (const auto& hwmon : fs::directory_iterator("/sys/class/hwmon")) {
            const std::string base = hwmon.path().string();
            const std::string sensorName = readFile(base + "/name");

            for (int j = 1; j <= 16; ++j) {
                const std::string inputPath = base + "/temp" + std::to_string(j) + "_input";
                const std::string labelPath = base + "/temp" + std::to_string(j) + "_label";
                if (!fs::exists(inputPath)) {
                    continue;
                }

                const std::string rawValue = readFile(inputPath);
                const std::string label = readFile(labelPath);
                if (rawValue.empty()) {
                    continue;
                }

                try {
                    const double c = std::stod(rawValue) / 1000.0;
                    std::string displayName = !label.empty() ? (sensorName + "/" + label) : (sensorName + "/temp" + std::to_string(j));

                    bool duplicate = false;
                    for (const auto& existing : temps) {
                        if (std::abs(existing.celsius - c) < 0.1 && existing.name.find(sensorName) != std::string::npos) {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate && c > 0 && c < 150) {
                        temps.push_back({displayName, c});
                    }
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }

    return temps;
}

std::vector<GPUInfo> SystemMetrics::getGPUInfo() {
    std::vector<GPUInfo> gpus;

    const std::string nvidia = execCommand(
        "nvidia-smi --query-gpu=name,driver_version,memory.total,temperature.gpu,utilization.gpu "
        "--format=csv,noheader,nounits 2>/dev/null");

    if (!nvidia.empty()) {
        std::istringstream iss(nvidia);
        std::string line;
        while (std::getline(iss, line)) {
            GPUInfo g;
            std::istringstream ls(line);
            std::string token;

            auto next = [&]() -> std::string {
                if (std::getline(ls, token, ',')) {
                    while (!token.empty() && token.front() == ' ') {
                        token.erase(token.begin());
                    }
                    return token;
                }
                return {};
            };

            g.name = next();
            g.driver = next();
            g.vram = next() + " MiB";
            g.temperature = next() + " \u00b0C";
            g.utilization = next() + "%";
            if (!g.name.empty()) {
                gpus.push_back(g);
            }
        }
    }

    if (gpus.empty()) {
        const std::string list = execCommand("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display'");
        if (!list.empty()) {
            std::istringstream iss(list);
            std::string line;
            while (std::getline(iss, line)) {
                GPUInfo g;
                const auto cp = line.find(": ");
                g.name = cp != std::string::npos ? line.substr(cp + 2) : line;
                gpus.push_back(g);
            }
        }
    }

    return gpus;
}

std::vector<NetworkInfo> SystemMetrics::getNetworkInfo() {
    std::vector<NetworkInfo> nets;

    try {
        for (const auto& entry : fs::directory_iterator("/sys/class/net")) {
            const std::string name = entry.path().filename().string();
            if (name == "lo") {
                continue;
            }

            NetworkInfo n;
            n.name = name;
            n.state = readFile(entry.path().string() + "/operstate");
            n.macAddress = readFile(entry.path().string() + "/address");

            const std::string speedRaw = readFile(entry.path().string() + "/speed");
            if (!speedRaw.empty() && speedRaw != "-1") {
                try {
                    const int mbps = std::stoi(speedRaw);
                    n.speed = mbps >= 1000 ? (std::to_string(mbps / 1000) + " Gbps") : (std::to_string(mbps) + " Mbps");
                } catch (...) {
                    n.speed = "N/A";
                }
            } else {
                n.speed = "N/A";
            }

            const std::string rx = readFile(entry.path().string() + "/statistics/rx_bytes");
            const std::string tx = readFile(entry.path().string() + "/statistics/tx_bytes");
            try {
                if (!rx.empty()) {
                    n.rxBytes = formatBytes(std::stoll(rx));
                }
                if (!tx.empty()) {
                    n.txBytes = formatBytes(std::stoll(tx));
                }
            } catch (...) {
            }

            nets.push_back(n);
        }
    } catch (...) {
    }

    return nets;
}

BatteryInfo SystemMetrics::getBatteryInfo() {
    BatteryInfo b;

    auto readDouble = [](const std::string& path) -> double {
        const std::string raw = readFile(path);
        if (raw.empty()) {
            return 0.0;
        }
        try {
            return std::stod(raw);
        } catch (...) {
            return 0.0;
        }
    };

    auto readInt = [](const std::string& path, int fallback = -1) -> int {
        const std::string raw = readFile(path);
        if (raw.empty()) {
            return fallback;
        }
        try {
            return std::stoi(raw);
        } catch (...) {
            return fallback;
        }
    };

    try {
        for (const auto& entry : fs::directory_iterator("/sys/class/power_supply")) {
            if (readFile(entry.path().string() + "/type") != "Battery") {
                continue;
            }

            b.hasBattery = true;
            const std::string cap = readFile(entry.path().string() + "/capacity");
            if (!cap.empty()) {
                try {
                    b.percent = std::stoi(cap);
                } catch (...) {
                }
            }
            b.status = readFile(entry.path().string() + "/status");
            b.health = readFile(entry.path().string() + "/health");

            const std::string base = entry.path().string();
            b.cycleCount = readInt(base + "/cycle_count", -1);

            // Batteries expose either charge_* (uAh) or energy_* (uWh). For display we normalize to Wh.
            const double chargeFullDesign_uAh = readDouble(base + "/charge_full_design");
            const double chargeFull_uAh = readDouble(base + "/charge_full");
            const double energyFullDesign_uWh = readDouble(base + "/energy_full_design");
            const double energyFull_uWh = readDouble(base + "/energy_full");
            const double voltageNow_uV = readDouble(base + "/voltage_now");

            if (energyFullDesign_uWh > 0.0 && energyFull_uWh > 0.0) {
                b.designCapacityWh = energyFullDesign_uWh / 1e6;
                b.fullCapacityWh = energyFull_uWh / 1e6;
            } else if (chargeFullDesign_uAh > 0.0 && chargeFull_uAh > 0.0 && voltageNow_uV > 0.0) {
                // Wh = Ah * V; convert uAh/uV to Ah/V first.
                const double voltageV = voltageNow_uV / 1e6;
                b.designCapacityWh = (chargeFullDesign_uAh / 1e6) * voltageV;
                b.fullCapacityWh = (chargeFull_uAh / 1e6) * voltageV;
            }

            if (b.designCapacityWh > 0.0) {
                b.fullCapacityPct = (b.fullCapacityWh / b.designCapacityWh) * 100.0;
            }
            break;
        }
    } catch (...) {
    }

    return b;
}

OSInfo SystemMetrics::getOSInfo() {
    OSInfo info;

    std::ifstream file("/etc/os-release");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                info.prettyName = line.substr(12);
                if (!info.prettyName.empty() && info.prettyName.front() == '"') {
                    info.prettyName.erase(info.prettyName.begin());
                }
                if (!info.prettyName.empty() && info.prettyName.back() == '"') {
                    info.prettyName.pop_back();
                }
                break;
            }
        }
    }

    struct utsname uts {};
    if (uname(&uts) == 0) {
        info.kernel = std::string(uts.sysname) + " " + uts.release;
        info.hostname = uts.nodename;
    }

    return info;
}

LoadAvg SystemMetrics::getLoadAverage() {
    LoadAvg la;
    std::ifstream file("/proc/loadavg");
    if (file.is_open()) {
        file >> la.avg1 >> la.avg5 >> la.avg15;
    }
    return la;
}

std::string SystemMetrics::getUptime() {
    std::ifstream file("/proc/uptime");
    double seconds = 0;
    if (file.is_open()) {
        file >> seconds;
    }

    int s = static_cast<int>(seconds);
    const int d = s / 86400;
    const int h = (s % 86400) / 3600;
    const int m = (s % 3600) / 60;
    s = s % 60;

    std::ostringstream out;
    if (d > 0) {
        out << d << "d ";
    }
    out << std::setfill('0') << std::setw(2) << h << ":" << std::setfill('0') << std::setw(2) << m << ":" << std::setfill('0')
        << std::setw(2) << s;
    return out.str();
}

int SystemMetrics::getProcessCount() {
    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            const std::string name = entry.path().filename().string();
            if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                ++count;
            }
        }
    } catch (...) {
    }
    return count;
}

std::vector<ProcessInfo> SystemMetrics::getTopProcesses(int count) {
    std::vector<ProcessInfo> procs;
    const std::string output =
        execCommand("ps aux --sort=-%cpu --no-headers 2>/dev/null | head -" + std::to_string(count));

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string user;
        std::string pidS;
        std::string cpuS;
        std::string memS;
        std::string vsz;
        std::string rss;
        std::string tty;
        std::string stat;
        std::string start;
        std::string timeS;
        std::string cmd;

        ls >> user >> pidS >> cpuS >> memS >> vsz >> rss >> tty >> stat >> start >> timeS;
        std::getline(ls, cmd);
        while (!cmd.empty() && cmd.front() == ' ') {
            cmd.erase(cmd.begin());
        }
        if (cmd.length() > 35) {
            cmd = cmd.substr(0, 32) + "...";
        }

        ProcessInfo p;
        try {
            p.pid = std::stoi(pidS);
            p.cpuPct = std::stod(cpuS);
            p.memPct = std::stod(memS);
        } catch (...) {
            continue;
        }
        p.name = cmd;
        procs.push_back(p);
    }

    return procs;
}

}  // namespace pcm
