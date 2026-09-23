#ifndef SOCIAL_NETWORK_H
#define SOCIAL_NETWORK_H

#include "User.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Outcome of operations that can fail. Nothing in the core prints to the console
// (except the explicit display* helpers); callers decide how to report problems.
enum class NetResult {
    Ok,
    UserNotFound,
    SameUser,
    AlreadyConnected,
    NotConnected,
    UsernameTaken,
    InvalidInput,
};

const char* describe(NetResult result);

struct NetworkStats {
    int users = 0;
    long long connections = 0;
    double averageFriends = 0.0;
    int maxFriends = 0;
    int isolatedUsers = 0;
};

struct FriendSuggestion {
    int userId = 0;
    int mutualFriends = 0;
    int sharedInterests = 0;
    double score = 0.0;
};

// An undirected friendship graph designed to hold millions of users in memory.
//
// Storage is contiguous and indexed by user id (ids start at 1 and are never
// reused), and every friend list is kept sorted, so membership checks are
// O(log d) and common-friend queries are a linear merge.
class SocialNetwork
{
public:
    SocialNetwork();

    // user management
    // Returns the new user's id, or -1 if the username is empty or already taken.
    int addUser(const std::string& username, const std::string& name, int age, const std::vector<std::string>& interests = {});
    NetResult removeUser(int id);
    User* getUser(int id);
    const User* getUser(int id) const;
    bool userExists(int id) const;
    int findUserByUsername(const std::string& username) const;  // -1 when not found
    int userCount() const { return userCount_; }
    long long connectionCount() const { return connectionCount_; }
    int maxUserId() const { return (int)users_.size() - 1; }  // live ids are in [1, maxUserId()]
    void displayUserProfile(int id) const;
    void reserve(size_t users);
    void clear();

    // connection management
    NetResult addConnection(int id1, int id2);
    NetResult removeConnection(int id1, int id2);
    bool areFriends(int id1, int id2) const;
    const std::vector<int>& getFriends(int id) const;  // sorted by id; empty for unknown ids
    int degree(int id) const { return userExists(id) ? (int)adjacency_[id].size() : 0; }
    void displayFriends(int id) const;

    // Bulk loading for importers and generators. Restores a user with a fixed id
    // (returns false if the id is taken or the username is not unique), and adds
    // many connections at once, skipping invalid pairs and duplicates.
    bool restoreUser(const User& user);
    long long addConnectionsBulk(const std::vector<std::pair<int, int>>& connections);

    // graph algorithms
    std::vector<int> getShortestPath(int startId, int endId) const;  // bidirectional BFS
    std::vector<int> findCommonFriends(int id1, int id2) const;
    std::vector<FriendSuggestion> suggestFriends(int id, size_t limit = 10) const;
    NetworkStats getStats() const;

    // data retrieval
    std::vector<User> getAllUsersData() const;
    std::vector<std::pair<int, int>> getAllConnectionsData() const;

    template <class Fn>
    void forEachUser(Fn&& fn) const {
        for (size_t id = 1; id < users_.size(); ++id)
            if (alive_[id]) fn(users_[id]);
    }

private:
    std::vector<User> users_;               // index == id; slot 0 unused
    std::vector<char> alive_;
    std::vector<std::vector<int>> adjacency_;
    // Username -> id index: open addressing over ids, keyed by the usernames
    // already stored in users_, so each entry costs 4 bytes instead of a
    // second copy of the string plus a hash-map node.
    std::vector<int> usernameSlots_;  // 0 = empty, -1 = deleted, otherwise a user id
    size_t usernameSlotsUsed_ = 0;    // non-empty slots, including deleted markers
    int userCount_ = 0;
    long long connectionCount_ = 0;

    // Scratch space for searches, reused between queries (generation-stamped so
    // it never needs clearing). This makes const queries non-reentrant: do not
    // run them on one network from several threads at once.
    mutable std::vector<uint32_t> seenForward_, seenBackward_;
    mutable std::vector<int> parentForward_, parentBackward_, distForward_, distBackward_, scratchCount_;
    mutable uint32_t stamp_ = 0;

    uint32_t nextStamp() const;
    void growTo(size_t id);
    size_t usernameSlot(const std::string& username) const;  // slot holding it, or the first free one
    void indexUsername(int id);
    void unindexUsername(int id);
    void rebuildUsernameIndex(size_t capacity);
};

#endif
