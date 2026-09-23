#ifndef NETWORK_IO_H
#define NETWORK_IO_H

#include "SocialNetwork.h"

#include <filesystem>
#include <string>

// Reads and writes networks as ".sgraph" files: a small, line-oriented,
// tab-separated text format that streams quickly even at millions of rows.
//
//   SOCIALGRAPH 1
//   U <id> <username> <name> <age> <interest;interest;...>
//   E <id> <id>
//
// Fields are separated by single tabs. Tabs, newlines and ';' inside values are
// replaced with spaces (',' for ';') on save, so every file round-trips.
namespace netio {

struct Result {
    bool ok = true;
    std::string error;
};

Result save(const SocialNetwork& network, const std::filesystem::path& path);

// Replaces the contents of `network`. On failure `network` is left unchanged.
Result load(SocialNetwork& network, const std::filesystem::path& path);

}  // namespace netio

#endif
