#include "SystemMetrics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

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

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

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
#ifdef _WIN32
    numCores_ = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
#else
    numCores_ = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    const auto times = readProcStat();
    if (!times.empty()) {
        prevTotal_ = times[0];
        prevTimes_.assign(times.begin() + 1, times.end());
    }
#endif
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
#ifdef _WIN32
    FILETIME idleTime {};
    FILETIME kernelTime {};
    FILETIME userTime {};
    perCore.clear();

    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return -1.0;
    }

    auto ftToU64 = [](const FILETIME& ft) -> unsigned long long {
        ULARGE_INTEGER value {};
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return value.QuadPart;
    };

    CPUTimes cur{};
    cur.idle = static_cast<long long>(ftToU64(idleTime));
    const unsigned long long kernel = ftToU64(kernelTime);
    const unsigned long long user = ftToU64(userTime);
    cur.system = static_cast<long long>(kernel > static_cast<unsigned long long>(cur.idle) ? kernel - cur.idle : 0ULL);
    cur.user = static_cast<long long>(user);

    if (firstRead_) {
        prevTotal_ = cur;
        firstRead_ = false;
        return -1.0;
    }

    const long long td = cur.total() - prevTotal_.total();
    const long long id = cur.totalIdle() - prevTotal_.totalIdle();

    double totalUsage = -1.0;
    if (td > 0) {
        totalUsage = (1.0 - static_cast<double>(id) / static_cast<double>(td)) * 100.0;
    }

    if (totalUsage >= 0.0) {
        perCore.assign(static_cast<size_t>(std::max(1, numCores_)), totalUsage);
    }

    prevTotal_ = cur;
    return totalUsage;
#else
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
#endif
}

CPUInfo SystemMetrics::getCPUInfo() {
    CPUInfo info;
#ifdef _WIN32
    info.logicalCores = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    info.modelName = execCommand("powershell -NoProfile -Command \"(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)\"");
    if (info.modelName.empty()) {
        info.modelName = "Windows CPU";
    }

    const std::string cores = execCommand(
        "powershell -NoProfile -Command \"(Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum\"");
    if (!cores.empty()) {
        try {
            info.physicalCores = std::stoi(cores);
        } catch (...) {
        }
    }
    if (info.physicalCores <= 0) {
        info.physicalCores = info.logicalCores;
    }

    const std::string maxMhz =
        execCommand("powershell -NoProfile -Command \"(Get-CimInstance Win32_Processor | Measure-Object -Property MaxClockSpeed -Average).Average\"");
    if (!maxMhz.empty()) {
        try {
            info.maxMHz = std::stod(maxMhz);
        } catch (...) {
        }
    }

    const std::string l3Kb = execCommand(
        "powershell -NoProfile -Command \"(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty L3CacheSize)\"");
    if (!l3Kb.empty()) {
        info.cacheSize = l3Kb + " KB";
    }

    SYSTEM_INFO si {};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            info.architecture = "x64";
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            info.architecture = "ARM64";
            break;
        default:
            info.architecture = "Unknown";
            break;
    }

    return info;
#else
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
#endif
}

double SystemMetrics::getCurrentAvgMHz(int logicalCores) {
#ifdef _WIN32
    (void)logicalCores;
    const std::string cur =
        execCommand("powershell -NoProfile -Command \"(Get-CimInstance Win32_Processor | Measure-Object -Property CurrentClockSpeed -Average).Average\"");
    if (cur.empty()) {
        return 0.0;
    }
    try {
        return std::stod(cur);
    } catch (...) {
        return 0.0;
    }
#else
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
#endif
}

MemoryInfo SystemMetrics::getMemoryInfo() {
    MemoryInfo info;
#ifdef _WIN32
    MEMORYSTATUSEX mem {};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        return info;
    }

    auto toGB = [](unsigned long long bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); };

    info.totalGB = toGB(mem.ullTotalPhys);
    info.availableGB = toGB(mem.ullAvailPhys);
    info.usedGB = info.totalGB - info.availableGB;
    info.swapTotalGB = toGB(mem.ullTotalPageFile);
    info.swapUsedGB = toGB(mem.ullTotalPageFile - mem.ullAvailPageFile);
    info.usagePercent = info.totalGB > 0.0 ? (info.usedGB / info.totalGB) * 100.0 : 0.0;
    return info;
#else
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
#endif
}

std::vector<DiskInfo> SystemMetrics::getDiskInfo() {
    std::vector<DiskInfo> disks;
#ifdef _WIN32
    char drives[256] = {};
    const DWORD len = GetLogicalDriveStringsA(static_cast<DWORD>(sizeof(drives)), drives);
    if (len == 0 || len > sizeof(drives)) {
        return disks;
    }

    for (const char* d = drives; *d != '\0'; d += std::strlen(d) + 1) {
        if (GetDriveTypeA(d) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER freeAvail {};
        ULARGE_INTEGER total {};
        ULARGE_INTEGER freeTotal {};
        if (!GetDiskFreeSpaceExA(d, &freeAvail, &total, &freeTotal) || total.QuadPart == 0) {
            continue;
        }

        DiskInfo info;
        info.mountPoint = d;
        info.device = d;
        info.filesystem = "NTFS/ReFS";
        info.totalGB = static_cast<double>(total.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        info.freeGB = static_cast<double>(freeTotal.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        info.usedGB = info.totalGB - info.freeGB;
        info.usagePercent = info.totalGB > 0.0 ? (info.usedGB / info.totalGB) * 100.0 : 0.0;
        disks.push_back(info);
    }
    return disks;
#else
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
#endif
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

#ifdef _WIN32
    const std::string output = execCommand(
        "powershell -NoProfile -Command \"Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + '|' + $_.DriverVersion + '|' + $_.AdapterRAM }\"");
    if (!output.empty()) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) {
                continue;
            }

            GPUInfo g;
            std::istringstream ls(line);
            std::getline(ls, g.name, '|');
            std::getline(ls, g.driver, '|');
            std::string ram;
            std::getline(ls, ram, '|');
            if (!ram.empty()) {
                try {
                    const auto bytes = std::stoull(ram);
                    g.vram = formatBytes(static_cast<long long>(bytes));
                } catch (...) {
                }
            }
            if (!g.name.empty()) {
                gpus.push_back(g);
            }
        }
    }
    return gpus;
#else

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
#endif
}

std::vector<NetworkInfo> SystemMetrics::getNetworkInfo() {
    std::vector<NetworkInfo> nets;

#ifdef _WIN32
    const std::string output = execCommand(
        "powershell -NoProfile -Command \"Get-NetAdapter -Physical | ForEach-Object { $_.Name + '|' + $_.Status + '|' + $_.LinkSpeed + '|' + $_.MacAddress }\"");
    if (!output.empty()) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) {
                continue;
            }

            NetworkInfo n;
            std::istringstream ls(line);
            std::getline(ls, n.name, '|');
            std::getline(ls, n.state, '|');
            std::getline(ls, n.speed, '|');
            std::getline(ls, n.macAddress, '|');
            if (!n.name.empty()) {
                nets.push_back(n);
            }
        }
    }
    return nets;
#else

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
#endif
}

BatteryInfo SystemMetrics::getBatteryInfo() {
    BatteryInfo b;

#ifdef _WIN32
    const std::string output = execCommand(
        "powershell -NoProfile -Command \"Get-CimInstance Win32_Battery | Select-Object -First 1 EstimatedChargeRemaining,BatteryStatus | ForEach-Object { $_.EstimatedChargeRemaining.ToString() + '|' + $_.BatteryStatus.ToString() }\"");
    if (!output.empty()) {
        std::istringstream ls(output);
        std::string percent;
        std::string status;
        std::getline(ls, percent, '|');
        std::getline(ls, status, '|');

        b.hasBattery = true;
        try {
            b.percent = std::stoi(percent);
        } catch (...) {
        }
        if (status == "1") {
            b.status = "Discharging";
        } else if (status == "2") {
            b.status = "AC";
        } else if (status == "6") {
            b.status = "Charging";
        } else {
            b.status = "Unknown";
        }
    }
    return b;
#else

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
#endif
}

OSInfo SystemMetrics::getOSInfo() {
    OSInfo info;

#ifdef _WIN32
    info.prettyName = execCommand("powershell -NoProfile -Command \"(Get-CimInstance Win32_OperatingSystem).Caption\"");
    if (info.prettyName.empty()) {
        info.prettyName = "Windows";
    }
    info.kernel = execCommand("cmd /c ver");

    char host[256] = {};
    DWORD hostLen = static_cast<DWORD>(sizeof(host));
    if (GetComputerNameA(host, &hostLen)) {
        info.hostname = host;
    }
    return info;
#else

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
#endif
}

LoadAvg SystemMetrics::getLoadAverage() {
#ifdef _WIN32
    return {};
#else
    LoadAvg la;
    std::ifstream file("/proc/loadavg");
    if (file.is_open()) {
        file >> la.avg1 >> la.avg5 >> la.avg15;
    }
    return la;
#endif
}

std::string SystemMetrics::getUptime() {
#ifdef _WIN32
    const unsigned long long seconds = GetTickCount64() / 1000ULL;
    int s = static_cast<int>(seconds);
#else
    std::ifstream file("/proc/uptime");
    double seconds = 0;
    if (file.is_open()) {
        file >> seconds;
    }

    int s = static_cast<int>(seconds);
#endif
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
#ifdef _WIN32
    int count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 pe {};
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(snapshot, &pe)) {
        do {
            ++count;
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return count;
#else
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
#endif
}

std::vector<ProcessInfo> SystemMetrics::getTopProcesses(int count) {
    std::vector<ProcessInfo> procs;
#ifdef _WIN32
    const std::string cmd =
        "powershell -NoProfile -Command \"Get-Process | Sort-Object CPU -Descending | Select-Object -First " +
        std::to_string(count) +
        " Id,ProcessName,CPU,WorkingSet64 | ForEach-Object { $_.Id.ToString() + '|' + $_.ProcessName + '|' + ($_.CPU.ToString()) + '|' + $_.WorkingSet64.ToString() }\"";
    const std::string output = execCommand(cmd);

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) {
            continue;
        }

        ProcessInfo p;
        std::string cpu;
        std::string mem;
        std::istringstream ls(line);
        std::string pid;
        std::getline(ls, pid, '|');
        std::getline(ls, p.name, '|');
        std::getline(ls, cpu, '|');
        std::getline(ls, mem, '|');

        try {
            p.pid = std::stoi(pid);
            p.cpuPct = cpu.empty() ? 0.0 : std::stod(cpu);
            p.memPct = 0.0;
        } catch (...) {
            continue;
        }
        procs.push_back(p);
    }
    return procs;
#else
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
#endif
}

}  // namespace pcm
