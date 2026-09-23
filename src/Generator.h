#ifndef GENERATOR_H
#define GENERATOR_H

#include "SocialNetwork.h"

#include <cstdint>

// Builds synthetic social networks with a realistic shape: people cluster into
// communities that share interests, friend counts are heavy-tailed (a few very
// popular people, many with a handful of friends, capped at 5,000 like
// Facebook), and a share of friendships bridge communities with a
// "rich get richer" bias.
namespace gen {

struct Options {
    int users = 1000;
    double averageFriends = 12.0;
    uint64_t seed = 42;
};

// Replaces the contents of `network` with a generated network.
void generate(SocialNetwork& network, const Options& options);

}  // namespace gen

#endif
