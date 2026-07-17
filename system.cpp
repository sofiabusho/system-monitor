#include "header.h"
#include <cstring>
#include <cctype>
#include <cstdlib>

// Fallback CPU brand string via CPUID (used if /proc/cpuinfo has no model name)
string CPUinfo()
{
    char CPUBrandString[0x40];
    unsigned int CPUInfo[4] = {0, 0, 0, 0};

    __cpuid(0x80000000, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
    unsigned int nExIds = CPUInfo[0];

    memset(CPUBrandString, 0, sizeof(CPUBrandString));

    for (unsigned int i = 0x80000000; i <= nExIds; ++i)
    {
        __cpuid(i, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);

        if (i == 0x80000002)
            memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
        else if (i == 0x80000003)
            memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
        else if (i == 0x80000004)
            memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
    }
    return string(CPUBrandString);
}

const char *getOsName()
{
#ifdef _WIN32
    return "Windows 32-bit";
#elif _WIN64
    return "Windows 64-bit";
#elif __APPLE__ || __MACH__
    return "Mac OSX";
#elif __linux__
    return "Linux";
#elif __FreeBSD__
    return "FreeBSD";
#elif __unix || __unix__
    return "Unix";
#else
    return "Other";
#endif
}

string readLoggedInUser()
{
    if (const char *envUser = getenv("USER"))
    {
        if (envUser[0] != '\0')
            return string(envUser);
    }

    if (char *login = getlogin())
    {
        if (login[0] != '\0')
            return string(login);
    }

    return "unknown";
}

string readHostname()
{
    char buf[HOST_NAME_MAX + 1];
    if (gethostname(buf, sizeof(buf)) == 0)
    {
        buf[sizeof(buf) - 1] = '\0';
        return string(buf);
    }
    return "unknown";
}

string readCpuModelName()
{
    ifstream cpuinfo("/proc/cpuinfo");
    if (!cpuinfo.is_open())
        return CPUinfo();

    string line;
    while (getline(cpuinfo, line))
    {
        const string key = "model name";
        if (line.compare(0, key.size(), key) != 0)
            continue;

        size_t colon = line.find(':');
        if (colon == string::npos)
            continue;

        string model = line.substr(colon + 1);
        size_t start = model.find_first_not_of(" \t");
        if (start == string::npos)
            return CPUinfo();
        return model.substr(start);
    }

    return CPUinfo();
}

static bool isNumericDirName(const char *name)
{
    if (name == nullptr || name[0] == '\0')
        return false;
    for (const char *p = name; *p; ++p)
    {
        if (!isdigit(static_cast<unsigned char>(*p)))
            return false;
    }
    return true;
}

TaskCounts countTasksByState()
{
    TaskCounts counts = {};
    DIR *proc = opendir("/proc");
    if (!proc)
        return counts;

    while (dirent *entry = readdir(proc))
    {
        if (!isNumericDirName(entry->d_name))
            continue;

        string path = string("/proc/") + entry->d_name + "/stat";
        ifstream statFile(path);
        if (!statFile.is_open())
            continue;

        string content;
        getline(statFile, content);
        // Format: pid (comm) state ... — state is the first char after the last ')'
        size_t closeParen = content.rfind(')');
        if (closeParen == string::npos || closeParen + 2 >= content.size())
            continue;

        char state = content[closeParen + 2];
        counts.total++;

        switch (state)
        {
        case 'R':
            counts.running++;
            break;
        case 'S':
            counts.sleeping++;
            break;
        case 'D':
            counts.uninterruptible++;
            break;
        case 'Z':
            counts.zombie++;
            break;
        case 'T':
        case 't':
            counts.stopped++;
            break;
        case 'I':
            counts.idle++;
            break;
        default:
            break;
        }
    }

    closedir(proc);
    return counts;
}

bool readCpuTimes(CPUStats &out)
{
    ifstream stat("/proc/stat");
    if (!stat.is_open())
        return false;

    string label;
    stat >> label;
    if (label != "cpu")
        return false;

    out = {};
    // Fields after "cpu": user nice system idle iowait irq softirq steal guest guest_nice
    if (!(stat >> out.user >> out.nice >> out.system >> out.idle))
        return false;

    // Remaining fields are optional on older kernels
    stat >> out.iowait >> out.irq >> out.softirq >> out.steal >> out.guest >> out.guestNice;
    return true;
}

static long long cpuBusyTicks(const CPUStats &s)
{
    return s.user + s.nice + s.system + s.irq + s.softirq + s.steal;
}

static long long cpuTotalTicks(const CPUStats &s)
{
    return cpuBusyTicks(s) + s.idle + s.iowait;
}

float sampleCpuUsagePercent()
{
    static bool havePrev = false;
    static CPUStats prev = {};

    CPUStats now = {};
    if (!readCpuTimes(now))
        return 0.0f;

    if (!havePrev)
    {
        prev = now;
        havePrev = true;
        return 0.0f;
    }

    const long long busyDelta = cpuBusyTicks(now) - cpuBusyTicks(prev);
    const long long totalDelta = cpuTotalTicks(now) - cpuTotalTicks(prev);
    prev = now;

    if (totalDelta <= 0)
        return 0.0f;

    float pct = (100.0f * static_cast<float>(busyDelta)) / static_cast<float>(totalDelta);
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 100.0f)
        pct = 100.0f;
    return pct;
}

static bool tryReadIbmFan(FanReading &out)
{
    ifstream fan("/proc/acpi/ibm/fan");
    if (!fan.is_open())
        return false;

    out.available = true;
    out.enabled = false;
    out.speedRpm = 0;
    out.level = "n/a";

    string key;
    while (fan >> key)
    {
        if (key == "status:")
        {
            string status;
            fan >> status;
            out.enabled = (status == "enabled" || status == "active");
        }
        else if (key == "speed:")
        {
            fan >> out.speedRpm;
        }
        else if (key == "level:")
        {
            fan >> out.level;
        }
        else
        {
            // skip rest of unknown line
            string rest;
            getline(fan, rest);
        }
    }
    return true;
}

static bool tryReadHwmonFan(FanReading &out)
{
    DIR *hwmonRoot = opendir("/sys/class/hwmon");
    if (!hwmonRoot)
        return false;

    while (dirent *hw = readdir(hwmonRoot))
    {
        if (hw->d_name[0] == '.')
            continue;

        string base = string("/sys/class/hwmon/") + hw->d_name;
        for (int i = 1; i <= 8; ++i)
        {
            string path = base + "/fan" + to_string(i) + "_input";
            ifstream in(path);
            int rpm = 0;
            if (!(in >> rpm))
                continue;

            out.available = true;
            out.speedRpm = rpm;
            out.enabled = rpm > 0;
            out.level = "n/a";

            string levelPath = base + "/pwm" + to_string(i);
            ifstream levelIn(levelPath);
            int pwm = 0;
            if (levelIn >> pwm)
                out.level = to_string(pwm);

            closedir(hwmonRoot);
            return true;
        }
    }

    closedir(hwmonRoot);
    return false;
}

FanReading readFanState()
{
    FanReading reading = {};
    reading.available = false;
    reading.enabled = false;
    reading.speedRpm = 0;
    reading.level = "unavailable";

    if (tryReadIbmFan(reading))
        return reading;
    if (tryReadHwmonFan(reading))
        return reading;
    return reading;
}

static bool tryReadIbmThermal(ThermalReading &out)
{
    ifstream thermal("/proc/acpi/ibm/thermal");
    if (!thermal.is_open())
        return false;

    string label;
    thermal >> label; // "temperatures:"
    float first = 0.0f;
    if (!(thermal >> first))
        return false;

    out.available = true;
    out.celsius = first;
    return true;
}

static bool tryReadHwmonThermal(ThermalReading &out)
{
    DIR *hwmonRoot = opendir("/sys/class/hwmon");
    if (!hwmonRoot)
        return false;

    float fallback = -1.0f;

    while (dirent *hw = readdir(hwmonRoot))
    {
        if (hw->d_name[0] == '.')
            continue;

        string base = string("/sys/class/hwmon/") + hw->d_name;
        for (int i = 1; i <= 8; ++i)
        {
            string inputPath = base + "/temp" + to_string(i) + "_input";
            ifstream input(inputPath);
            int milli = 0;
            if (!(input >> milli) || milli <= 0)
                continue;

            float celsius = milli / 1000.0f;
            if (fallback < 0.0f)
                fallback = celsius;

            string labelPath = base + "/temp" + to_string(i) + "_label";
            ifstream labelFile(labelPath);
            string label;
            if (labelFile && getline(labelFile, label))
            {
                if (label.find("Package") != string::npos ||
                    label.find("Tctl") != string::npos ||
                    label.find("CPU") != string::npos)
                {
                    out.available = true;
                    out.celsius = celsius;
                    closedir(hwmonRoot);
                    return true;
                }
            }
        }
    }

    closedir(hwmonRoot);

    if (fallback >= 0.0f)
    {
        out.available = true;
        out.celsius = fallback;
        return true;
    }
    return false;
}

ThermalReading readThermalState()
{
    ThermalReading reading = {};
    reading.available = false;
    reading.celsius = 0.0f;

    if (tryReadIbmThermal(reading))
        return reading;
    if (tryReadHwmonThermal(reading))
        return reading;
    return reading;
}
