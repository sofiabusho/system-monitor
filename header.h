// To make sure you don't declare the function more than once by including the header multiple times.
#ifndef header_H
#define header_H

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <dirent.h>
#include <vector>
#include <iostream>
#include <cmath>
// lib to read from file
#include <fstream>
// for the name of the computer and the logged in user
#include <unistd.h>
#include <limits.h>
// this is for us to get the cpu information
// mostly in unix system
// not sure if it will work in windows
#include <cpuid.h>
// this is for the memory usage and other memory visualization
// for linux gotta find a way for windows
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
// for time and date
#include <ctime>
// ifconfig ip addresses
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>

using namespace std;

struct CPUStats
{
    long long int user;
    long long int nice;
    long long int system;
    long long int idle;
    long long int iowait;
    long long int irq;
    long long int softirq;
    long long int steal;
    long long int guest;
    long long int guestNice;
};

// processes `stat`
struct Proc
{
    int pid;
    string name;
    char state;
    long long int vsize;
    long long int rss;
    long long int utime;
    long long int stime;
};

struct IP4
{
    string name;
    char addressBuffer[INET_ADDRSTRLEN];
};

struct Networks
{
    vector<IP4> ip4s;
};

// Matches /proc/net/dev Receive columns
struct NetRecvCounters
{
    unsigned long long bytes;
    unsigned long long packets;
    unsigned long long errs;
    unsigned long long drop;
    unsigned long long fifo;
    unsigned long long frame;
    unsigned long long compressed;
    unsigned long long multicast;
};

// Matches /proc/net/dev Transmit columns
struct NetXmitCounters
{
    unsigned long long bytes;
    unsigned long long packets;
    unsigned long long errs;
    unsigned long long drop;
    unsigned long long fifo;
    unsigned long long colls;
    unsigned long long carrier;
    unsigned long long compressed;
};

struct NetIfaceStats
{
    string name;
    NetRecvCounters rx;
    NetXmitCounters tx;
};

// system facts (OS, user, host, tasks, CPU model)
struct TaskCounts
{
    int total;
    int running;
    int sleeping;
    int uninterruptible;
    int zombie;
    int stopped;
    int idle;
};

struct FanReading
{
    bool available;
    bool enabled;
    int speedRpm;
    string level;
};

struct ThermalReading
{
    bool available;
    float celsius;
};

string CPUinfo();
const char *getOsName();
string readLoggedInUser();
string readHostname();
string readCpuModelName();
TaskCounts countTasksByState();
bool readCpuTimes(CPUStats &out);
float sampleCpuUsagePercent();
FanReading readFanState();
ThermalReading readThermalState();

// memory / disk / process table
struct MemSnapshot
{
    float totalRamMB;
    float usedRamMB;
    float totalSwapMB;
    float usedSwapMB;
};

struct DiskSnapshot
{
    float totalGB;
    float usedGB;
};

struct ProcessRow
{
    int pid;
    string name;
    string state;
    float cpuPercent;
    float memPercent;
};

MemSnapshot readMemSnapshot();
DiskSnapshot readDiskSnapshot(const char *mountPath = "/");
vector<ProcessRow> collectProcessRows();

Networks collectIpv4Addresses();
vector<NetIfaceStats> collectNetIfaceStats();
string formatByteSize(unsigned long long bytes);

#endif
