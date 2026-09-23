// Unit tests for the Social Graph core. Build and run:
//   g++ -std=c++17 -O2 -Isrc tests/test_core.cpp src/*.cpp (minus WindowsApp.cpp and main.cpp)
// or via CMake: ctest

#include "Analytics.h"
#include "ForceLayout.h"
#include "Generator.h"
#include "NetworkIO.h"
#include "SocialNetwork.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                     \
    static void name();                                \
    static Register register_##name(#name, name);      \
    static void name()

// Reference single-source BFS distance, for checking the bidirectional search.
int referenceDistance(const SocialNetwork& net, int from, int to) {
    if (!net.userExists(from) || !net.userExists(to)) return -1;
    std::vector<int> dist(net.maxUserId() + 1, -1);
    std::queue<int> q;
    dist[from] = 0;
    q.push(from);
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (u == to) return dist[u];
        for (int v : net.getFriends(u)) {
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                q.push(v);
            }
        }
    }
    return -1;
}

SocialNetwork smallNetwork() {
    // 1-2-3 triangle, 3-4, 4-5 chain, 6 isolated
    SocialNetwork net;
    for (int i = 1; i <= 6; ++i) net.addUser("user" + std::to_string(i), "User " + std::to_string(i), 20 + i, {"Chess"});
    net.addConnection(1, 2);
    net.addConnection(2, 3);
    net.addConnection(1, 3);
    net.addConnection(3, 4);
    net.addConnection(4, 5);
    return net;
}

}  // namespace

TEST(users_are_added_with_sequential_ids) {
    SocialNetwork net;
    CHECK_EQ(net.addUser("alice", "Alice", 30, {"Tea"}), 1);
    CHECK_EQ(net.addUser("bob", "Bob", 40), 2);
    CHECK_EQ(net.userCount(), 2);
    CHECK(net.getUser(1) != nullptr);
    CHECK_EQ(net.getUser(1)->name, std::string("Alice"));
    CHECK(net.getUser(3) == nullptr);
    CHECK(net.getUser(0) == nullptr);
    CHECK(net.getUser(-5) == nullptr);
}

TEST(usernames_must_be_unique_and_non_empty) {
    SocialNetwork net;
    CHECK_EQ(net.addUser("alice", "Alice", 30), 1);
    CHECK_EQ(net.addUser("alice", "Another Alice", 31), -1);
    CHECK_EQ(net.addUser("", "Nobody", 31), -1);
    CHECK_EQ(net.findUserByUsername("alice"), 1);
    CHECK_EQ(net.findUserByUsername("missing"), -1);
    CHECK_EQ(net.userCount(), 1);
}

TEST(connections_report_precise_results) {
    SocialNetwork net = smallNetwork();
    CHECK(net.addConnection(1, 1) == NetResult::SameUser);
    CHECK(net.addConnection(1, 99) == NetResult::UserNotFound);
    CHECK(net.addConnection(1, 2) == NetResult::AlreadyConnected);
    CHECK(net.addConnection(2, 1) == NetResult::AlreadyConnected);
    CHECK(net.addConnection(1, 6) == NetResult::Ok);
    CHECK(net.removeConnection(1, 6) == NetResult::Ok);
    CHECK(net.removeConnection(1, 6) == NetResult::NotConnected);
    CHECK(net.removeConnection(1, 99) == NetResult::UserNotFound);
    CHECK_EQ(net.connectionCount(), 5);
    CHECK(std::string(describe(NetResult::AlreadyConnected)).size() > 0);
}

TEST(friend_lists_are_sorted_and_symmetric) {
    SocialNetwork net;
    for (int i = 1; i <= 5; ++i) net.addUser("u" + std::to_string(i), "U", 20);
    net.addConnection(3, 5);
    net.addConnection(3, 1);
    net.addConnection(3, 4);
    const auto& f = net.getFriends(3);
    CHECK(std::is_sorted(f.begin(), f.end()));
    CHECK_EQ(f.size(), 3u);
    CHECK(net.areFriends(5, 3));
    CHECK(net.areFriends(3, 5));
    CHECK(!net.areFriends(1, 5));
    CHECK_EQ(net.degree(3), 3);
    CHECK(net.getFriends(42).empty());
}

TEST(shortest_path_basics) {
    SocialNetwork net = smallNetwork();
    CHECK(net.getShortestPath(1, 1) == std::vector<int>{1});
    std::vector<int> p = net.getShortestPath(1, 5);
    CHECK((p == std::vector<int>{1, 3, 4, 5}));
    CHECK(net.getShortestPath(1, 6).empty());
    CHECK(net.getShortestPath(1, 99).empty());
}

TEST(bidirectional_bfs_matches_reference_on_random_graphs) {
    std::mt19937 rng(7);
    for (int graph = 0; graph < 20; ++graph) {
        SocialNetwork net;
        int n = 30 + graph * 15;
        for (int i = 1; i <= n; ++i) net.addUser("u" + std::to_string(i), "U", 20);
        int edges = n + (int)(rng() % (unsigned)(n * 2));
        for (int e = 0; e < edges; ++e) net.addConnection(1 + (int)(rng() % n), 1 + (int)(rng() % n));

        for (int q = 0; q < 60; ++q) {
            int a = 1 + (int)(rng() % n), b = 1 + (int)(rng() % n);
            std::vector<int> path = net.getShortestPath(a, b);
            int expected = referenceDistance(net, a, b);
            if (expected == -1) {
                CHECK(path.empty());
                continue;
            }
            CHECK_EQ((int)path.size() - 1, expected);
            CHECK_EQ(path.front(), a);
            CHECK_EQ(path.back(), b);
            for (size_t i = 0; i + 1 < path.size(); ++i) CHECK(net.areFriends(path[i], path[i + 1]));
        }
    }
}

TEST(common_friends_match_set_intersection) {
    std::mt19937 rng(11);
    SocialNetwork net;
    int n = 200;
    for (int i = 1; i <= n; ++i) net.addUser("u" + std::to_string(i), "U", 20);
    for (int e = 0; e < 2000; ++e) net.addConnection(1 + (int)(rng() % n), 1 + (int)(rng() % n));
    for (int q = 0; q < 200; ++q) {
        int a = 1 + (int)(rng() % n), b = 1 + (int)(rng() % n);
        std::vector<int> got = net.findCommonFriends(a, b);
        std::vector<int> expected;
        if (a != b) {
            std::set<int> fa(net.getFriends(a).begin(), net.getFriends(a).end());
            for (int f : net.getFriends(b))
                if (fa.count(f)) expected.push_back(f);
        }
        CHECK(got == expected);
    }
    CHECK(net.findCommonFriends(1, 1).empty());
}

TEST(remove_user_cleans_up_everything) {
    SocialNetwork net = smallNetwork();
    CHECK(net.removeUser(3) == NetResult::Ok);
    CHECK(!net.userExists(3));
    CHECK_EQ(net.userCount(), 5);
    CHECK_EQ(net.connectionCount(), 2);  // 1-2 and 4-5 remain
    CHECK(!net.areFriends(1, 3));
    for (int id = 1; id <= net.maxUserId(); ++id)
        for (int f : net.getFriends(id)) CHECK(f != 3);
    CHECK(net.getShortestPath(1, 5).empty());
    CHECK(net.removeUser(3) == NetResult::UserNotFound);
    CHECK_EQ(net.findUserByUsername("user3"), -1);
    // ids are never reused, and the freed username can be taken again
    CHECK_EQ(net.addUser("user3", "New", 20), 7);
}

TEST(friend_suggestions_rank_by_mutual_friends) {
    SocialNetwork net;
    for (int i = 1; i <= 6; ++i) net.addUser("u" + std::to_string(i), "U", 20, {i % 2 ? "Chess" : "Golf"});
    // 1 is friends with 2 and 3. 4 knows both 2 and 3; 5 knows only 2.
    net.addConnection(1, 2);
    net.addConnection(1, 3);
    net.addConnection(4, 2);
    net.addConnection(4, 3);
    net.addConnection(5, 2);
    auto s = net.suggestFriends(1, 10);
    CHECK_EQ(s.size(), 2u);
    CHECK_EQ(s[0].userId, 4);
    CHECK_EQ(s[0].mutualFriends, 2);
    CHECK_EQ(s[1].userId, 5);
    CHECK_EQ(s[1].sharedInterests, 1);  // both like Chess
    for (const auto& x : s) CHECK(x.userId != 1 && !net.areFriends(1, x.userId));
    // Nobody reachable: fall back to shared interests.
    auto lonely = net.suggestFriends(6, 10);
    CHECK(!lonely.empty());
    for (const auto& x : lonely) CHECK(x.sharedInterests > 0);
    CHECK(net.suggestFriends(99).empty());
}

TEST(stats_summarize_the_network) {
    SocialNetwork net = smallNetwork();
    NetworkStats s = net.getStats();
    CHECK_EQ(s.users, 6);
    CHECK_EQ(s.connections, 5);
    CHECK(std::fabs(s.averageFriends - 10.0 / 6.0) < 1e-9);
    CHECK_EQ(s.maxFriends, 3);
    CHECK_EQ(s.isolatedUsers, 1);
}

TEST(bulk_connections_skip_invalid_and_duplicates) {
    SocialNetwork net;
    for (int i = 1; i <= 4; ++i) net.addUser("u" + std::to_string(i), "U", 20);
    long long added = net.addConnectionsBulk({{1, 2}, {2, 1}, {1, 2}, {3, 3}, {1, 99}, {3, 4}});
    CHECK_EQ(added, 2);
    CHECK_EQ(net.connectionCount(), 2);
    CHECK(std::is_sorted(net.getFriends(1).begin(), net.getFriends(1).end()));
}

TEST(save_and_load_round_trip) {
    SocialNetwork net = smallNetwork();
    net.addUser("tricky", "Name\twith\ttabs", 33, {"a;b", "Rock & Roll"});
    net.removeUser(2);  // leaves a gap in the ids
    auto path = std::filesystem::temp_directory_path() / "socialgraph_roundtrip.sgraph";
    CHECK(netio::save(net, path).ok);

    SocialNetwork loaded;
    netio::Result r = netio::load(loaded, path);
    CHECK(r.ok);
    CHECK_EQ(loaded.userCount(), net.userCount());
    CHECK_EQ(loaded.connectionCount(), net.connectionCount());
    CHECK(!loaded.userExists(2));
    const User* t = loaded.getUser(7);
    CHECK(t != nullptr);
    if (t) {
        CHECK_EQ(t->name, std::string("Name with tabs"));
        CHECK_EQ(t->interests.size(), 2u);
        CHECK_EQ(t->interests[0], std::string("a,b"));
    }
    CHECK(loaded.getAllConnectionsData() == net.getAllConnectionsData());
    // new users continue after the highest id
    CHECK_EQ(loaded.addUser("next", "Next", 20), 8);
    std::filesystem::remove(path);
}

TEST(load_rejects_bad_files_and_keeps_existing_data) {
    auto path = std::filesystem::temp_directory_path() / "socialgraph_bad.sgraph";
    SocialNetwork net = smallNetwork();

    { std::ofstream(path) << "hello world\n"; }
    netio::Result r = netio::load(net, path);
    CHECK(!r.ok);
    CHECK_EQ(net.userCount(), 6);

    { std::ofstream(path) << "SOCIALGRAPH 1\nU\t1\ta\tA\t20\t\nU\t1\tb\tB\t20\t\n"; }
    r = netio::load(net, path);
    CHECK(!r.ok);
    CHECK(r.error.find("Line 3") != std::string::npos);

    { std::ofstream(path) << "SOCIALGRAPH 1\nU\tx\ta\tA\t20\t\n"; }
    CHECK(!netio::load(net, path).ok);

    CHECK(!netio::load(net, std::filesystem::temp_directory_path() / "does_not_exist.sgraph").ok);
    CHECK_EQ(net.userCount(), 6);
    std::filesystem::remove(path);
}

TEST(components_and_communities) {
    // Two 6-cliques joined by a single bridge, plus an isolated person.
    SocialNetwork net;
    for (int i = 1; i <= 13; ++i) net.addUser("u" + std::to_string(i), "U", 20);
    for (int base : {1, 7})
        for (int a = base; a < base + 6; ++a)
            for (int b = a + 1; b < base + 6; ++b) net.addConnection(a, b);
    net.addConnection(6, 7);

    auto comps = analytics::connectedComponents(net);
    CHECK_EQ(comps.count, 2);
    CHECK_EQ(comps.largestSize, 12);
    CHECK(comps.componentOf[1] == comps.componentOf[12]);
    CHECK(comps.componentOf[13] != comps.componentOf[1]);

    auto comm = analytics::detectCommunities(net);
    CHECK_EQ(comm.count, 2);
    CHECK(comm.communityOf[1] == comm.communityOf[5]);
    CHECK(comm.communityOf[8] == comm.communityOf[12]);
    CHECK(comm.communityOf[1] != comm.communityOf[12]);
    CHECK_EQ(comm.communityOf[13], -1);

    auto top = analytics::mostConnected(net, 2);
    CHECK_EQ(top.size(), 2u);
    CHECK((top[0] == 6 && top[1] == 7));
}

TEST(generator_builds_valid_networks) {
    SocialNetwork net;
    gen::generate(net, {5000, 12.0, 3});
    CHECK_EQ(net.userCount(), 5000);
    NetworkStats s = net.getStats();
    CHECK(s.averageFriends > 6 && s.averageFriends < 20);
    CHECK(s.maxFriends <= 5000);
    for (int id = 1; id <= net.maxUserId(); ++id) {
        const auto& f = net.getFriends(id);
        CHECK(std::is_sorted(f.begin(), f.end()));
        CHECK(std::adjacent_find(f.begin(), f.end()) == f.end());
        for (int x : f) {
            if (x == id || !net.areFriends(x, id)) { CHECK(false); break; }
        }
    }
    // deterministic for a given seed
    SocialNetwork again;
    gen::generate(again, {5000, 12.0, 3});
    CHECK(again.getAllConnectionsData() == net.getAllConnectionsData());
}

TEST(force_layout_settles_and_stays_finite) {
    ForceLayout layout;
    int n = 300;
    layout.bodies.resize(n);
    for (int i = 0; i < n; ++i) layout.seed(i);
    std::mt19937 rng(5);
    for (int i = 1; i < n; ++i) layout.links.emplace_back(i, (int)(rng() % (unsigned)i));  // random tree
    int ticks = 0;
    while (layout.active() && ticks < 1000) {
        layout.tick();
        ++ticks;
    }
    CHECK(!layout.active());
    double linked = 0;
    for (const auto& l : layout.links) {
        const auto& a = layout.bodies[l.first];
        const auto& b = layout.bodies[l.second];
        CHECK(std::isfinite(a.x) && std::isfinite(a.y));
        linked += std::hypot(a.x - b.x, a.y - b.y);
    }
    linked /= layout.links.size();
    std::printf("    mean link length %.1f after %d ticks\n", linked, ticks);
    CHECK(linked > 30 && linked < 400);  // springs roughly honored
}

int main() {
    for (const auto& t : registry()) {
        int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "[ ok ]" : "[FAIL]", t.name);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
