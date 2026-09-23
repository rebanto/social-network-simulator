#ifndef ANALYTICS_H
#define ANALYTICS_H

#include "SocialNetwork.h"

#include <cstdint>
#include <utility>
#include <vector>

// Whole-network analysis. Every function here only reads the network and uses
// no shared scratch space, so it is safe to run on a background thread as long
// as nobody modifies the network meanwhile.
namespace analytics {

struct Components {
    std::vector<int> componentOf;  // indexed by user id; -1 for unused ids
    int count = 0;
    int largestSize = 0;
    int largestComponent = -1;
};

// Groups people into connected components (who can reach whom at all).
Components connectedComponents(const SocialNetwork& network);

struct Communities {
    std::vector<int> communityOf;  // indexed by user id; -1 for unused ids or people with no friends
    int count = 0;                 // communities with at least two members
    std::vector<std::pair<int, int>> largest;  // (community, size), biggest first, at most 20
};

// Finds tightly-knit friend groups with label propagation: everyone repeatedly
// adopts the group label most common among their friends. Runs in roughly
// O(iterations * connections). Community ids are ordered by size (0 = largest).
Communities detectCommunities(const SocialNetwork& network, int maxIterations = 15, uint64_t seed = 1);

// The `count` people with the most friends, most connected first.
std::vector<int> mostConnected(const SocialNetwork& network, size_t count);

}  // namespace analytics

#endif
