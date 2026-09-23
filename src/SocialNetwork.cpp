#include "SocialNetwork.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>

using namespace std;

const char* describe(NetResult result) {
    switch (result) {
    case NetResult::Ok: return "OK";
    case NetResult::UserNotFound: return "One or more users do not exist.";
    case NetResult::SameUser: return "A user cannot be friends with themselves.";
    case NetResult::AlreadyConnected: return "Users are already connected.";
    case NetResult::NotConnected: return "Users are not friends.";
    case NetResult::UsernameTaken: return "That username is already taken.";
    case NetResult::InvalidInput: return "Invalid input.";
    }
    return "Unknown error.";
}

SocialNetwork::SocialNetwork() {
    clear();
}

void SocialNetwork::clear() {
    users_.assign(1, User());
    alive_.assign(1, 0);
    adjacency_.assign(1, vector<int>());
    usernameSlots_.assign(16, 0);
    usernameSlotsUsed_ = 0;
    userCount_ = 0;
    connectionCount_ = 0;
    seenForward_.clear();
    seenBackward_.clear();
    stamp_ = 0;
}

void SocialNetwork::reserve(size_t users) {
    users_.reserve(users + 1);
    alive_.reserve(users + 1);
    adjacency_.reserve(users + 1);
    size_t capacity = usernameSlots_.size();
    while (capacity < users * 2) capacity *= 2;
    if (capacity != usernameSlots_.size()) rebuildUsernameIndex(capacity);
}

void SocialNetwork::growTo(size_t id) {
    if (users_.size() <= id) {
        users_.resize(id + 1);
        alive_.resize(id + 1, 0);
        adjacency_.resize(id + 1);
    }
}

static uint64_t hashUsername(const string& s) {  // FNV-1a
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

size_t SocialNetwork::usernameSlot(const string& username) const {
    size_t mask = usernameSlots_.size() - 1;
    size_t i = (size_t)hashUsername(username) & mask;
    size_t firstDeleted = SIZE_MAX;
    while (true) {
        int id = usernameSlots_[i];
        if (id == 0) return firstDeleted != SIZE_MAX ? firstDeleted : i;
        if (id == -1) {
            if (firstDeleted == SIZE_MAX) firstDeleted = i;
        } else if (users_[id].username == username) {
            return i;
        }
        i = (i + 1) & mask;
    }
}

void SocialNetwork::rebuildUsernameIndex(size_t capacity) {
    usernameSlots_.assign(capacity, 0);
    usernameSlotsUsed_ = 0;
    for (size_t id = 1; id < users_.size(); ++id) {
        if (alive_[id]) {
            indexUsername((int)id);
        }
    }
}

void SocialNetwork::indexUsername(int id) {
    if ((usernameSlotsUsed_ + 1) * 2 > usernameSlots_.size()) {
        // grow (or just clear out deleted markers when mostly tombstones)
        size_t capacity = usernameSlots_.size();
        while ((size_t)(userCount_ + 1) * 2 > capacity / 2) capacity *= 2;
        rebuildUsernameIndex(capacity);
    }
    size_t slot = usernameSlot(users_[id].username);
    if (usernameSlots_[slot] == 0) usernameSlotsUsed_++;
    usernameSlots_[slot] = id;
}

void SocialNetwork::unindexUsername(int id) {
    size_t slot = usernameSlot(users_[id].username);
    if (usernameSlots_[slot] == id) usernameSlots_[slot] = -1;
}

uint32_t SocialNetwork::nextStamp() const {
    size_t n = users_.size();
    if (seenForward_.size() != n) {
        seenForward_.assign(n, 0);
        seenBackward_.assign(n, 0);
        parentForward_.resize(n);
        parentBackward_.resize(n);
        distForward_.resize(n);
        distBackward_.resize(n);
        scratchCount_.resize(n);
        stamp_ = 0;
    }
    if (++stamp_ == 0) {  // wrapped around: reset so stale marks can't collide
        fill(seenForward_.begin(), seenForward_.end(), 0);
        fill(seenBackward_.begin(), seenBackward_.end(), 0);
        stamp_ = 1;
    }
    return stamp_;
}

// ---------------------------------------------------------------------------
// User management
// ---------------------------------------------------------------------------

int SocialNetwork::addUser(const string& username, const string& name, int age, const vector<string>& interests) {
    if (username.empty() || findUserByUsername(username) != -1) {
        return -1;
    }

    int newUserId = (int)users_.size();
    User newUserObject = User(newUserId, username, name, age, interests);
    if (!restoreUser(newUserObject)) {
        return -1;
    }
    return newUserId;
}

bool SocialNetwork::restoreUser(const User& user) {
    if (user.id <= 0 || user.username.empty() || findUserByUsername(user.username) != -1) {
        return false;
    }
    growTo(user.id);
    if (alive_[user.id]) {
        return false;
    }

    users_[user.id] = user;
    alive_[user.id] = 1;
    adjacency_[user.id].clear();
    userCount_++;
    indexUsername(user.id);
    return true;
}

NetResult SocialNetwork::removeUser(int id) {
    if (!userExists(id)) {
        return NetResult::UserNotFound;
    }

    for (int friendId : adjacency_[id]) {
        auto& friendsOfFriend = adjacency_[friendId];
        auto it = lower_bound(friendsOfFriend.begin(), friendsOfFriend.end(), id);
        if (it != friendsOfFriend.end() && *it == id) {
            friendsOfFriend.erase(it);
        }
    }
    connectionCount_ -= (long long)adjacency_[id].size();
    vector<int>().swap(adjacency_[id]);

    unindexUsername(id);
    users_[id] = User();
    alive_[id] = 0;
    userCount_--;
    return NetResult::Ok;
}

User* SocialNetwork::getUser(int id) {
    return userExists(id) ? &users_[id] : nullptr;
}

const User* SocialNetwork::getUser(int id) const {
    return userExists(id) ? &users_[id] : nullptr;
}

bool SocialNetwork::userExists(int id) const {
    return id > 0 && id < (int)users_.size() && alive_[id];
}

int SocialNetwork::findUserByUsername(const string& username) const {
    int id = usernameSlots_[usernameSlot(username)];
    return id > 0 ? id : -1;
}

void SocialNetwork::displayUserProfile(int id) const {
    const User* user_ptr = getUser(id);

    if (user_ptr != nullptr) {
        user_ptr->displayProfile();
    } else {
        cout << "\nError retrieving user with id: " << id << "." << endl;
    }
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

NetResult SocialNetwork::addConnection(int id1, int id2) {
    if (id1 == id2) {
        return NetResult::SameUser;
    }
    if (!userExists(id1) || !userExists(id2)) {
        return NetResult::UserNotFound;
    }

    auto& friends1 = adjacency_[id1];
    auto it1 = lower_bound(friends1.begin(), friends1.end(), id2);
    if (it1 != friends1.end() && *it1 == id2) {
        return NetResult::AlreadyConnected;
    }
    friends1.insert(it1, id2);

    auto& friends2 = adjacency_[id2];
    friends2.insert(lower_bound(friends2.begin(), friends2.end(), id1), id1);

    connectionCount_++;
    return NetResult::Ok;
}

NetResult SocialNetwork::removeConnection(int id1, int id2) {
    if (!userExists(id1) || !userExists(id2)) {
        return NetResult::UserNotFound;
    }

    auto& friends1 = adjacency_[id1];
    auto it1 = lower_bound(friends1.begin(), friends1.end(), id2);
    if (it1 == friends1.end() || *it1 != id2) {
        return NetResult::NotConnected;
    }
    friends1.erase(it1);

    auto& friends2 = adjacency_[id2];
    auto it2 = lower_bound(friends2.begin(), friends2.end(), id1);
    if (it2 != friends2.end() && *it2 == id1) {
        friends2.erase(it2);
    }

    connectionCount_--;
    return NetResult::Ok;
}

bool SocialNetwork::areFriends(int id1, int id2) const {
    if (!userExists(id1) || !userExists(id2)) {
        return false;
    }
    // search the shorter list
    const auto& a = adjacency_[id1].size() <= adjacency_[id2].size() ? adjacency_[id1] : adjacency_[id2];
    int other = &a == &adjacency_[id1] ? id2 : id1;
    return binary_search(a.begin(), a.end(), other);
}

const vector<int>& SocialNetwork::getFriends(int id) const {
    static const vector<int> none;
    return userExists(id) ? adjacency_[id] : none;
}

void SocialNetwork::displayFriends(int id) const {
    const User* user = getUser(id);

    if (user == nullptr) {
        cout << "\nUser does not exist." << endl;
    } else {
        cout << "\nFriends of " << user->name << ":" << endl;

        const auto& friendsOfUser = adjacency_[id];
        if (friendsOfUser.empty()) {
            cout << "No friends found." << endl << endl;
        } else {
            for (int friendId : friendsOfUser) {
                cout << users_[friendId].name << endl;
            }
        }
    }
}

long long SocialNetwork::addConnectionsBulk(const vector<pair<int, int>>& connections) {
    vector<char> touched(users_.size(), 0);
    for (const auto& c : connections) {
        int a = c.first, b = c.second;
        if (a == b || !userExists(a) || !userExists(b)) {
            continue;
        }
        adjacency_[a].push_back(b);
        adjacency_[b].push_back(a);
        touched[a] = touched[b] = 1;
    }

    long long before = connectionCount_;
    long long totalDegree = 0;
    for (size_t id = 1; id < adjacency_.size(); ++id) {
        auto& friends = adjacency_[id];
        if (touched[id]) {
            sort(friends.begin(), friends.end());
            friends.erase(unique(friends.begin(), friends.end()), friends.end());
            friends.shrink_to_fit();
        }
        totalDegree += (long long)friends.size();
    }
    connectionCount_ = totalDegree / 2;
    return connectionCount_ - before;
}

// ---------------------------------------------------------------------------
// Graph algorithms
// ---------------------------------------------------------------------------

vector<int> SocialNetwork::getShortestPath(int startId, int endId) const {
    if (!userExists(startId) || !userExists(endId)) {
        return {};
    }

    if (startId == endId) {
        return {startId};
    }

    // Bidirectional BFS: grow a frontier from each end, always expanding the
    // smaller one, until they touch. On social graphs this visits a tiny
    // fraction of the nodes a one-sided BFS would.
    uint32_t st = nextStamp();
    seenForward_[startId] = st;  parentForward_[startId] = -1; distForward_[startId] = 0;
    seenBackward_[endId] = st;   parentBackward_[endId] = -1;  distBackward_[endId] = 0;

    vector<int> forwardFrontier{startId}, backwardFrontier{endId}, next;
    int meet = -1, best = INT_MAX;

    while (!forwardFrontier.empty() && !backwardFrontier.empty() && meet == -1) {
        bool forward = forwardFrontier.size() <= backwardFrontier.size();
        auto& frontier = forward ? forwardFrontier : backwardFrontier;
        auto& seenHere = forward ? seenForward_ : seenBackward_;
        auto& seenThere = forward ? seenBackward_ : seenForward_;
        auto& parentHere = forward ? parentForward_ : parentBackward_;
        auto& distHere = forward ? distForward_ : distBackward_;
        auto& distThere = forward ? distBackward_ : distForward_;

        next.clear();
        for (int currentId : frontier) {
            for (int neighborId : adjacency_[currentId]) {
                if (seenHere[neighborId] == st) {
                    continue;
                }
                seenHere[neighborId] = st;
                parentHere[neighborId] = currentId;
                distHere[neighborId] = distHere[currentId] + 1;
                next.push_back(neighborId);
                if (seenThere[neighborId] == st && distHere[neighborId] + distThere[neighborId] < best) {
                    best = distHere[neighborId] + distThere[neighborId];
                    meet = neighborId;
                }
            }
        }
        frontier.swap(next);
    }

    if (meet == -1) {
        return {};
    }

    vector<int> path;
    for (int id = meet; id != -1; id = parentForward_[id]) {
        path.push_back(id);
    }
    reverse(path.begin(), path.end());
    for (int id = parentBackward_[meet]; id != -1; id = parentBackward_[id]) {
        path.push_back(id);
    }
    return path;
}

vector<int> SocialNetwork::findCommonFriends(int id1, int id2) const {
    if (!userExists(id1) || !userExists(id2) || id1 == id2) {
        return {};
    }

    // Both lists are sorted, so a linear merge finds the intersection in O(n + m).
    const auto& a = adjacency_[id1];
    const auto& b = adjacency_[id2];
    vector<int> commonFriends;
    set_intersection(a.begin(), a.end(), b.begin(), b.end(), back_inserter(commonFriends));
    return commonFriends;
}

static bool equalsIgnoreCase(const string& a, const string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static int countSharedInterests(const User& a, const User& b) {
    int shared = 0;
    for (const auto& x : a.interests) {
        for (const auto& y : b.interests) {
            if (equalsIgnoreCase(x, y)) {
                shared++;
                break;
            }
        }
    }
    return shared;
}

vector<FriendSuggestion> SocialNetwork::suggestFriends(int id, size_t limit) const {
    if (!userExists(id) || limit == 0) {
        return {};
    }

    uint32_t st = nextStamp();
    // seenBackward_ marks people to exclude (self and existing friends);
    // seenForward_ marks candidates whose mutual-friend count is in scratchCount_.
    seenBackward_[id] = st;
    for (int f : adjacency_[id]) {
        seenBackward_[f] = st;
    }

    vector<int> candidates;
    for (int f : adjacency_[id]) {
        for (int g : adjacency_[f]) {
            if (seenBackward_[g] == st) {
                continue;
            }
            if (seenForward_[g] != st) {
                seenForward_[g] = st;
                scratchCount_[g] = 0;
                candidates.push_back(g);
            }
            scratchCount_[g]++;
        }
    }

    const User& me = users_[id];
    vector<FriendSuggestion> suggestions;

    if (candidates.empty()) {
        // No friends-of-friends yet: fall back to people with shared interests.
        for (size_t other = 1; other < users_.size(); ++other) {
            if (!alive_[other] || seenBackward_[other] == st) {
                continue;
            }
            int shared = countSharedInterests(me, users_[other]);
            if (shared > 0) {
                suggestions.push_back({(int)other, 0, shared, 0.5 * shared});
            }
        }
    } else {
        // Keep the strongest candidates by mutual friends before the (costlier)
        // interest comparison, so hubs with huge second-degree circles stay fast.
        size_t keep = max<size_t>(limit * 5, 50);
        if (candidates.size() > keep) {
            nth_element(candidates.begin(), candidates.begin() + keep, candidates.end(),
                        [this](int a, int b) { return scratchCount_[a] > scratchCount_[b]; });
            candidates.resize(keep);
        }
        for (int c : candidates) {
            int shared = countSharedInterests(me, users_[c]);
            suggestions.push_back({c, scratchCount_[c], shared, scratchCount_[c] + 0.5 * shared});
        }
    }

    sort(suggestions.begin(), suggestions.end(), [](const FriendSuggestion& a, const FriendSuggestion& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.userId < b.userId;
    });
    if (suggestions.size() > limit) {
        suggestions.resize(limit);
    }
    return suggestions;
}

NetworkStats SocialNetwork::getStats() const {
    NetworkStats stats;
    stats.users = userCount_;
    stats.connections = connectionCount_;
    stats.averageFriends = userCount_ ? (2.0 * connectionCount_) / userCount_ : 0.0;
    for (size_t id = 1; id < users_.size(); ++id) {
        if (!alive_[id]) {
            continue;
        }
        int d = (int)adjacency_[id].size();
        stats.maxFriends = max(stats.maxFriends, d);
        if (d == 0) {
            stats.isolatedUsers++;
        }
    }
    return stats;
}

// ---------------------------------------------------------------------------
// Data retrieval
// ---------------------------------------------------------------------------

vector<User> SocialNetwork::getAllUsersData() const {
    vector<User> resultVector;
    resultVector.reserve(userCount_);
    forEachUser([&](const User& user) { resultVector.push_back(user); });
    return resultVector;
}

vector<pair<int, int>> SocialNetwork::getAllConnectionsData() const {
    vector<pair<int, int>> resultVector;
    resultVector.reserve((size_t)connectionCount_);

    for (size_t userId = 1; userId < adjacency_.size(); ++userId) {
        for (int friendId : adjacency_[userId]) {
            if ((int)userId < friendId) {
                resultVector.emplace_back((int)userId, friendId);
            }
        }
    }
    return resultVector;
}
