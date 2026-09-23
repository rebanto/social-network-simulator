#include "Generator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

using namespace std;

namespace gen {

namespace {

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {  // splitmix64
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return (int)(next() % (uint64_t)n); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

const char* kFirstNames[] = {
    "Aaliyah", "Aarav", "Ada", "Aiko", "Alice", "Amara", "Andre", "Anya", "Ben", "Bianca", "Carlos", "Chen",
    "Chloe", "Dan", "Diego", "Elena", "Emeka", "Emily", "Farah", "Felix", "Grace", "Hana", "Hugo", "Ines",
    "Ivan", "Jada", "Jonas", "Julia", "Kai", "Kofi", "Lars", "Lena", "Leo", "Lucia", "Maya", "Marco",
    "Mateo", "Mei", "Mia", "Nadia", "Nina", "Noah", "Omar", "Olivia", "Priya", "Quinn", "Rafael", "Rin",
    "Rosa", "Sam", "Sara", "Sofia", "Tariq", "Theo", "Uma", "Vera", "Wei", "Yara", "Yusuf", "Zoe",
};
const char* kLastNames[] = {
    "Adams", "Ahmed", "Alvarez", "Andersen", "Asante", "Brown", "Chen", "Costa", "Dubois", "Eze", "Fischer",
    "Garcia", "Gupta", "Haddad", "Hansen", "Ito", "Johnson", "Kim", "Kowalski", "Kumar", "Lee", "Lopez",
    "Martin", "Martins", "Meyer", "Moreau", "Nakamura", "Nguyen", "Novak", "Okafor", "Olsen", "Patel",
    "Petrov", "Rossi", "Santos", "Schmidt", "Silva", "Singh", "Smith", "Song", "Tanaka", "Taylor", "Wang",
    "Weber", "Williams", "Wozniak", "Yilmaz", "Zhang",
};
const char* kInterests[] = {
    "Anime", "Astronomy", "Baking", "Baseball", "Basketball", "Biking", "Board games", "Books", "Camping",
    "Chess", "Climbing", "Coding", "Coffee", "Cooking", "Dance", "Design", "Drawing", "Fashion", "Film",
    "Fishing", "Football", "Gaming", "Gardening", "Golf", "Guitar", "Hiking", "History", "Jazz", "Knitting",
    "Languages", "Martial arts", "Music", "Painting", "Photography", "Piano", "Podcasts", "Poetry",
    "Politics", "Running", "Sailing", "Science", "Skiing", "Soccer", "Surfing", "Swimming", "Tennis",
    "Travel", "Volunteering", "Wine", "Writing", "Yoga",
};

template <class T, size_t N>
constexpr int countOf(T (&)[N]) { return (int)N; }

string lower(const char* s) {
    string out(s);
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

}  // namespace

void generate(SocialNetwork& network, const Options& options) {
    const int n = max(0, options.users);
    Rng rng(options.seed);
    network.clear();
    network.reserve((size_t)n);
    if (n == 0) return;

    // Communities: mostly small circles, occasionally large ones.
    vector<int> communityOf(n + 1), communityStart, communitySize;
    vector<array<int, 4>> communityInterests;
    for (int id = 1; id <= n;) {
        double r = rng.unit();
        int size = r < 0.7 ? 15 + rng.below(60) : (r < 0.95 ? 80 + rng.below(250) : 400 + rng.below(1200));
        size = min(size, n - id + 1);
        array<int, 4> favorite;
        for (int& f : favorite) f = rng.below(countOf(kInterests));
        communityStart.push_back(id);
        communitySize.push_back(size);
        communityInterests.push_back(favorite);
        for (int i = 0; i < size; ++i) communityOf[id + i] = (int)communityStart.size() - 1;
        id += size;
    }

    // People
    for (int id = 1; id <= n; ++id) {
        const char* first = kFirstNames[rng.below(countOf(kFirstNames))];
        const char* last = kLastNames[rng.below(countOf(kLastNames))];
        int community = communityOf[id];
        int baseAge = 18 + (int)(community * 2654435761u % 40);

        vector<string> interests;
        int count = 1 + rng.below(3);
        for (int i = 0; i < count; ++i) {
            int pick = rng.unit() < 0.7 ? communityInterests[community][rng.below(4)] : rng.below(countOf(kInterests));
            string interest = kInterests[pick];
            if (find(interests.begin(), interests.end(), interest) == interests.end()) interests.push_back(interest);
        }

        User user(id, lower(first) + "." + lower(last) + to_string(id), string(first) + " " + last,
                  max(13, min(90, baseAge + rng.below(15) - 7)), interests);
        network.restoreUser(user);
    }

    // Friendships: heavy-tailed friend counts (Pareto, alpha = 2.5), capped at 5,000.
    const double alpha = 2.5;
    const double scale = max(1.0, options.averageFriends) * (alpha - 1) / alpha;
    const int cap = min(5000, max(1, n - 1));
    vector<pair<int, int>> edges;
    edges.reserve((size_t)(n * options.averageFriends / 2 * 1.1));

    for (int id = 1; id <= n; ++id) {
        double friends = scale / pow(1.0 - rng.unit(), 1.0 / alpha);
        int wanted = (int)min<double>(cap, friends / 2 + rng.unit());
        int community = communityOf[id];
        int start = communityStart[community], size = communitySize[community];
        for (int k = 0; k < wanted; ++k) {
            int other;
            if (size > 1 && rng.unit() < 0.8) {
                other = start + rng.below(size);
            } else if (!edges.empty() && rng.unit() < 0.6) {
                const auto& e = edges[rng.below((int)min<size_t>(edges.size(), 0x7FFFFFFF))];
                other = (rng.next() & 1) ? e.first : e.second;  // preferential attachment
            } else {
                other = 1 + rng.below(n);
            }
            if (other != id) edges.emplace_back(id, other);
        }
    }

    // Enforce the cap on both ends (preferential attachment can pile edges onto hubs).
    vector<int> degree(n + 1, 0);
    size_t kept = 0;
    for (const auto& e : edges) {
        if (degree[e.first] < cap && degree[e.second] < cap) {
            degree[e.first]++;
            degree[e.second]++;
            edges[kept++] = e;
        }
    }
    edges.resize(kept);

    network.addConnectionsBulk(edges);
}

}  // namespace gen
