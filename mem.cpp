#include "header.h"
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <sys/time.h>

static bool parseMeminfoValue(const string &line, const string &key, long &outKb)
{
    if (line.compare(0, key.size(), key) != 0)
        return false;

    istringstream iss(line.substr(key.size()));
    long value = 0;
    if (!(iss >> value))
        return false;
    outKb = value;
    return true;
}

MemSnapshot readMemSnapshot()
{
    MemSnapshot snap = {};
    ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open())
        return snap;

    long memTotal = 0;
    long memAvailable = 0;
    long swapTotal = 0;
    long swapFree = 0;

    string line;
    while (getline(meminfo, line))
    {
        parseMeminfoValue(line, "MemTotal:", memTotal);
        parseMeminfoValue(line, "MemAvailable:", memAvailable);
        parseMeminfoValue(line, "SwapTotal:", swapTotal);
        parseMeminfoValue(line, "SwapFree:", swapFree);
    }

    snap.totalRamMB = memTotal / 1024.0f;
    snap.usedRamMB = (memTotal > memAvailable) ? (memTotal - memAvailable) / 1024.0f : 0.0f;
    snap.totalSwapMB = swapTotal / 1024.0f;
    snap.usedSwapMB = (swapTotal > swapFree) ? (swapTotal - swapFree) / 1024.0f : 0.0f;
    return snap;
}

DiskSnapshot readDiskSnapshot(const char *mountPath)
{
    DiskSnapshot snap = {};
    struct statvfs vfs = {};
    if (statvfs(mountPath, &vfs) != 0)
        return snap;

    const double block = static_cast<double>(vfs.f_frsize);
    const double total = static_cast<double>(vfs.f_blocks) * block;
    const double freeBytes = static_cast<double>(vfs.f_bfree) * block;
    const double used = total - freeBytes;
    const double gib = 1024.0 * 1024.0 * 1024.0;

    snap.totalGB = static_cast<float>(total / gib);
    snap.usedGB = static_cast<float>((used > 0.0) ? used / gib : 0.0);
    return snap;
}

static bool isPidDirectory(const char *name)
{
    if (!name || !*name)
        return false;
    for (const char *p = name; *p; ++p)
    {
        if (!isdigit(static_cast<unsigned char>(*p)))
            return false;
    }
    return true;
}

static double monotonicSeconds()
{
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1e6;
}

static bool readProcessIdentity(int pid, string &name, string &state, long &rssKb)
{
    const string path = "/proc/" + to_string(pid) + "/status";
    ifstream status(path);
    if (!status.is_open())
        return false;

    name.clear();
    state.clear();
    rssKb = 0;

    string line;
    while (getline(status, line))
    {
        if (line.compare(0, 5, "Name:") == 0)
        {
            size_t start = line.find_first_not_of(" \t", 5);
            name = (start == string::npos) ? "" : line.substr(start);
        }
        else if (line.compare(0, 6, "State:") == 0)
        {
            size_t start = line.find_first_not_of(" \t", 6);
            state = (start == string::npos) ? "?" : line.substr(start);
        }
        else if (line.compare(0, 6, "VmRSS:") == 0)
        {
            istringstream iss(line.substr(6));
            iss >> rssKb;
        }
    }

    if (name.empty())
        name = "?";
    if (state.empty())
        state = "?";
    return true;
}

static bool readProcessCpuTicks(int pid, long long &cpuTicks)
{
    const string path = "/proc/" + to_string(pid) + "/stat";
    ifstream statFile(path);
    if (!statFile.is_open())
        return false;

    string content;
    getline(statFile, content);
    size_t closeParen = content.rfind(')');
    if (closeParen == string::npos)
        return false;

    istringstream iss(content.substr(closeParen + 2));
    char state = '?';
    long long ppid = 0, pgrp = 0, session = 0, tty = 0, tpgid = 0;
    unsigned long flags = 0, minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
    long long utime = 0, stime = 0;

    // After ") ": state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
    if (!(iss >> state >> ppid >> pgrp >> session >> tty >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime))
        return false;

    (void)state;
    cpuTicks = utime + stime;
    return true;
}

vector<ProcessRow> collectProcessRows()
{
    struct CpuSample
    {
        long long ticks;
        double atSeconds;
    };
    static map<int, CpuSample> previous;
    map<int, CpuSample> current;

    const long clockTicks = sysconf(_SC_CLK_TCK);
    const double now = monotonicSeconds();
    const long pageSize = sysconf(_SC_PAGESIZE);

    MemSnapshot mem = readMemSnapshot();
    const float memTotalKb = mem.totalRamMB * 1024.0f;

    vector<ProcessRow> rows;
    DIR *proc = opendir("/proc");
    if (!proc)
        return rows;

    while (dirent *entry = readdir(proc))
    {
        if (!isPidDirectory(entry->d_name))
            continue;

        const int pid = atoi(entry->d_name);
        string name;
        string state;
        long rssKb = 0;
        if (!readProcessIdentity(pid, name, state, rssKb))
            continue;

        long long ticks = 0;
        if (!readProcessCpuTicks(pid, ticks))
            continue;

        current[pid] = CpuSample{ticks, now};

        float cpuPercent = 0.0f;
        map<int, CpuSample>::const_iterator prevIt = previous.find(pid);
        if (prevIt != previous.end())
        {
            const double dt = now - prevIt->second.atSeconds;
            const long long dTicks = ticks - prevIt->second.ticks;
            if (dt > 0.0 && clockTicks > 0 && dTicks >= 0)
            {
                cpuPercent = static_cast<float>(
                    (100.0 * static_cast<double>(dTicks) / static_cast<double>(clockTicks)) / dt);
            }
        }

        float memPercent = 0.0f;
        if (memTotalKb > 0.0f)
        {
            // Prefer VmRSS; if missing, leave at 0
            (void)pageSize;
            memPercent = (100.0f * static_cast<float>(rssKb)) / memTotalKb;
        }

        ProcessRow row;
        row.pid = pid;
        row.name = name;
        row.state = state;
        row.cpuPercent = cpuPercent;
        row.memPercent = memPercent;
        rows.push_back(row);
    }

    closedir(proc);
    previous.swap(current);
    return rows;
}
