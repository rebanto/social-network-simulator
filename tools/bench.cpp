// Scale benchmark for the Social Graph core.
//
//   bench [users=1000000] [averageFriends=20]
//
// Generates a synthetic network, then times loading/saving and the queries the
// app runs interactively.

#include "Analytics.h"
#include "Generator.h"
#include "NetworkIO.h"
#include "SocialNetwork.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static double memoryMB() {
    PROCESS_MEMORY_COUNTERS pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? pmc.WorkingSetSize / 1048576.0 : 0.0;
}
#elif defined(__linux__)
#include <fstream>
#include <string>
static double memoryMB() {  // resident set size from /proc
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            double kb = 0;
            status >> kb;
            return kb / 1024.0;
        }
    }
    return -1.0;
}
#else
static double memoryMB() { return -1.0; }  // unknown on this platform
#endif

using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int main(int argc, char** argv) {
    int users = argc > 1 ? std::atoi(argv[1]) : 1000000;
    double avg = argc > 2 ? std::atof(argv[2]) : 20.0;

    std::printf("Social Graph core benchmark: %d people, ~%.0f friends each\n\n", users, avg);

    SocialNetwork net;
    auto t = Clock::now();
    gen::generate(net, {users, avg, 42});
    NetworkStats stats = net.getStats();
    std::printf("%-34s %10.0f ms\n", "generate network", msSince(t));
    std::printf("  %d people, %lld friendships, avg %.1f, max %d friends\n", stats.users, stats.connections,
                stats.averageFriends, stats.maxFriends);
    if (memoryMB() >= 0) std::printf("  memory in use: %.0f MB\n", memoryMB());
    std::printf("\n");

    auto path = std::filesystem::temp_directory_path() / "socialgraph_bench.sgraph";
    t = Clock::now();
    netio::save(net, path);
    double saveMs = msSince(t);
    std::printf("%-34s %10.0f ms  (%.0f MB file)\n", "save to disk", saveMs,
                std::filesystem::file_size(path) / 1048576.0);
    {
        SocialNetwork loaded;
        t = Clock::now();
        netio::Result r = netio::load(loaded, path);
        std::printf("%-34s %10.0f ms  %s\n", "load from disk", msSince(t), r.ok ? "" : r.error.c_str());
    }
    std::filesystem::remove(path);

    std::mt19937 rng(1);
    auto randomUser = [&] { return 1 + (int)(rng() % (unsigned)users); };
    const int queries = 1000;

    t = Clock::now();
    long long hops = 0, found = 0;
    for (int i = 0; i < queries; ++i) {
        auto p = net.getShortestPath(randomUser(), randomUser());
        if (!p.empty()) { hops += (long long)p.size() - 1; found++; }
    }
    double pathMs = msSince(t);
    std::printf("%-34s %10.3f ms  (avg %.2f degrees of separation)\n", "shortest path (per query)", pathMs / queries,
                found ? (double)hops / found : 0.0);

    t = Clock::now();
    size_t common = 0;
    for (int i = 0; i < queries; ++i) common += net.findCommonFriends(randomUser(), randomUser()).size();
    std::printf("%-34s %10.3f ms\n", "mutual friends (per query)", msSince(t) / queries);

    t = Clock::now();
    for (int i = 0; i < queries; ++i) net.suggestFriends(randomUser(), 10);
    std::printf("%-34s %10.3f ms\n", "friend suggestions (per query)", msSince(t) / queries);

    int hub = analytics::mostConnected(net, 1).front();
    t = Clock::now();
    net.suggestFriends(hub, 10);
    std::printf("%-34s %10.3f ms  (%d friends)\n", "suggestions for the biggest hub", msSince(t), net.degree(hub));

    t = Clock::now();
    auto top = analytics::mostConnected(net, 200);
    std::printf("%-34s %10.0f ms\n", "200 most connected", msSince(t));

    t = Clock::now();
    auto comps = analytics::connectedComponents(net);
    std::printf("%-34s %10.0f ms  (%d components, largest %d)\n", "connected components", msSince(t), comps.count,
                comps.largestSize);

    t = Clock::now();
    auto comm = analytics::detectCommunities(net);
    std::printf("%-34s %10.0f ms  (%d communities)\n", "community detection", msSince(t), comm.count);

    if (memoryMB() >= 0) std::printf("\nmemory in use: %.0f MB\n", memoryMB());
    (void)common;
    (void)top;
    return 0;
}
