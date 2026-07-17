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
