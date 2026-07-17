#include "header.h"
#include <cstdio>
#include <sstream>

string formatByteSize(unsigned long long bytes)
{
    const double kb = static_cast<double>(bytes) / 1024.0;
    const double mb = kb / 1024.0;
    const double gb = mb / 1024.0;

    char buf[64];
    // Prefer a unit that keeps the value readable (subject example: ~431.78 MB)
    if (gb >= 1.0)
        snprintf(buf, sizeof(buf), "%.2f GB", gb);
    else if (mb >= 1.0)
        snprintf(buf, sizeof(buf), "%.2f MB", mb);
    else
        snprintf(buf, sizeof(buf), "%.2f KB", kb);
    return string(buf);
}

Networks collectIpv4Addresses()
{
    Networks nets;
    struct ifaddrs *head = nullptr;
    if (getifaddrs(&head) == -1)
        return nets;

    for (struct ifaddrs *ifa = head; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        IP4 entry;
        entry.name = ifa->ifa_name ? ifa->ifa_name : "?";
        entry.addressBuffer[0] = '\0';

        const void *addr = &reinterpret_cast<sockaddr_in *>(ifa->ifa_addr)->sin_addr;
        if (!inet_ntop(AF_INET, addr, entry.addressBuffer, INET_ADDRSTRLEN))
            continue;

        nets.ip4s.push_back(entry);
    }

    freeifaddrs(head);
    return nets;
}

static string trimWhitespace(string s)
{
    const size_t start = s.find_first_not_of(" \t");
    if (start == string::npos)
        return "";
    const size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

vector<NetIfaceStats> collectNetIfaceStats()
{
    vector<NetIfaceStats> list;
    ifstream file("/proc/net/dev");
    if (!file.is_open())
        return list;

    string line;
    // Skip two header lines
    getline(file, line);
    getline(file, line);

    while (getline(file, line))
    {
        size_t colon = line.find(':');
        if (colon == string::npos)
            continue;

        NetIfaceStats iface;
        iface.name = trimWhitespace(line.substr(0, colon));
        if (iface.name.empty())
            continue;

        istringstream iss(line.substr(colon + 1));
        iface.rx = {};
        iface.tx = {};

        if (!(iss >> iface.rx.bytes >> iface.rx.packets >> iface.rx.errs >> iface.rx.drop >>
              iface.rx.fifo >> iface.rx.frame >> iface.rx.compressed >> iface.rx.multicast >>
              iface.tx.bytes >> iface.tx.packets >> iface.tx.errs >> iface.tx.drop >>
              iface.tx.fifo >> iface.tx.colls >> iface.tx.carrier >> iface.tx.compressed))
        {
            continue;
        }

        list.push_back(iface);
    }

    return list;
}
