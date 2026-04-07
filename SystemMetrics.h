#pragma once

#include <string>
#include <vector>

namespace pcm {

struct CPUTimes {
    long long user = 0;
    long long nice = 0;
    long long system = 0;
    long long idle = 0;
    long long iowait = 0;
    long long irq = 0;
    long long softirq = 0;
    long long steal = 0;

    long long totalIdle() const { return idle + iowait; }
    long long totalActive() const { return user + nice + system + irq + softirq + steal; }
    long long total() const { return totalIdle() + totalActive(); }
};

class CPUMonitor {
public:
    CPUMonitor();

    int getNumCores() const;
    double getUsage(std::vector<double>& perCore);

private:
    std::vector<CPUTimes> readProcStat() const;

    std::vector<CPUTimes> prevTimes_;
    CPUTimes prevTotal_;
    int numCores_ = 0;
    bool firstRead_ = true;
};

struct CPUInfo {
    std::string modelName;
    std::string architecture;
    std::string cacheSize;
    int physicalCores = 0;
    int logicalCores = 0;
    double maxMHz = 0.0;
};

struct MemoryInfo {
    double totalGB = 0.0;
    double usedGB = 0.0;
    double availableGB = 0.0;
    double buffCacheGB = 0.0;
    double swapTotalGB = 0.0;
    double swapUsedGB = 0.0;
    double usagePercent = 0.0;
};

struct DiskInfo {
    std::string mountPoint;
    std::string device;
    std::string filesystem;
    double totalGB = 0.0;
    double usedGB = 0.0;
    double freeGB = 0.0;
    double usagePercent = 0.0;
};

struct TempReading {
    std::string name;
    double celsius = 0.0;
};

struct GPUInfo {
    std::string name;
    std::string driver;
    std::string vram;
    std::string temperature;
    std::string utilization;
};

struct NetworkInfo {
    std::string name;
    std::string speed;
    std::string macAddress;
    std::string state;
    std::string rxBytes;
    std::string txBytes;
};

struct BatteryInfo {
    bool hasBattery = false;
    int percent = 0;
    std::string status;
    std::string health;
    int cycleCount = -1;
    double designCapacityWh = 0.0;
    double fullCapacityWh = 0.0;
    double fullCapacityPct = 0.0;
};

struct OSInfo {
    std::string prettyName;
    std::string kernel;
    std::string hostname;
};

struct LoadAvg {
    double avg1 = 0.0;
    double avg5 = 0.0;
    double avg15 = 0.0;
};

struct ProcessInfo {
    std::string name;
    double cpuPct = 0.0;
    double memPct = 0.0;
    int pid = 0;
};

class SystemMetrics {
public:
    static CPUInfo getCPUInfo();
    static double getCurrentAvgMHz(int logicalCores);
    static MemoryInfo getMemoryInfo();
    static std::vector<DiskInfo> getDiskInfo();
    static std::vector<TempReading> getTemperatures();
    static std::vector<GPUInfo> getGPUInfo();
    static std::vector<NetworkInfo> getNetworkInfo();
    static BatteryInfo getBatteryInfo();
    static OSInfo getOSInfo();
    static LoadAvg getLoadAverage();
    static std::string getUptime();
    static int getProcessCount();
    static std::vector<ProcessInfo> getTopProcesses(int count = 5);
};

}  // namespace pcm
