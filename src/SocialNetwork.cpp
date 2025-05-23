#include "SocialNetwork.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <set>
#include <map>
#include <vector>

using namespace std;

SocialNetwork::SocialNetwork() {
    nextUserId = 1;
}

int SocialNetwork::addUser(const string& username, const string& name, int age, const vector<string>& interests) {
    int newUserId = nextUserId;
    User newUserObject = User(newUserId, username, name, age, interests);

    users.insert({newUserId, newUserObject});
    adjacencyList[newUserId] = vector<int>();

    nextUserId++;
    return newUserId;
}

User* SocialNetwork::getUser(int id) {
    if (users.count(id)) {
        return &users[id];
    } else {
        return nullptr;
    }
}

void SocialNetwork::displayUserProfile(int id) {
    User* user_ptr = getUser(id);

    if (user_ptr != nullptr) {
        user_ptr->displayProfile();
    } else {
        cout << "\nError retrieving user with id: " << id << "." << endl;
    }
}

bool SocialNetwork::addConnection(int id1, int id2) {
    if (id1 == id2) {
        return false;
    }

    if (!users.count(id1) || !users.count(id2)) {
        cout << "One or more IDs provided does not exist." << endl;
        return false;
    }

    for (const auto& friendId : adjacencyList[id1]) {
        if (friendId == id2) {
            cout << "Users are already connected." << endl;
            return false;
        }
    }

    adjacencyList[id1].push_back(id2);
    adjacencyList[id2].push_back(id1);
    return true;
}

bool SocialNetwork::removeConnection(int id1, int id2) {
    if (!users.count(id1) || !users.count(id2)) {
        cout << "One or more IDs provided does not exist." << endl;
        return false;
    }

    auto& friends1 = adjacencyList.at(id1);
    auto it1 = std::find(friends1.begin(), friends1.end(), id2);
    if (it1 == friends1.end()) {
        cout << "Users are not friends." << endl;
        return false;
    }

    auto& friendsOfId1 = adjacencyList.at(id1);
    friendsOfId1.erase(std::remove(friendsOfId1.begin(), friendsOfId1.end(), id2), friendsOfId1.end());

    auto& friendsOfId2 = adjacencyList.at(id2);
    friendsOfId2.erase(std::remove(friendsOfId2.begin(), friendsOfId2.end(), id1), friendsOfId2.end());

    return true;
}

void SocialNetwork::displayFriends(int id) {
    User* user = getUser(id);
    
    if (user == nullptr) {
        cout << "\nUser does not exist." << endl;
    } else {
        cout << "\nFriends of " << user->name << ":" << endl;
        
        auto& friendsOfUser = adjacencyList.at(id);
        if (friendsOfUser.empty()) {
            cout << "No friends found." << endl << endl;
        } else {
            for (auto& friendId : friendsOfUser) {
                User* friendUser = getUser(friendId);
                cout << friendUser->name << endl;
            }
        }
    }
}

vector<int> SocialNetwork::getShortestPath(int startId, int endId) {
    if (!users.count(startId) || !users.count(endId)) {
        cout << "One or more IDs provided does not exist." << endl;
        return {};
    }

    if (startId == endId) {
        return {startId};
    }

    queue<int> q;
    set<int> visited;
    map<int, int> parent;

    q.push(startId);
    visited.insert(startId);

    while (!q.empty()) {
        int currentId = q.front();
        q.pop();

        if (currentId == endId) {
            break;
        }

        for (int neighborId : adjacencyList.at(currentId)) {
            if (!visited.count(neighborId)) {
                visited.insert(neighborId);
                parent[neighborId] = currentId;
                q.push(neighborId);
            }
        }
    }

    vector<int> path;
    if (parent.count(endId) == 0) {
        return {};
    }
    int currentId = endId;
    while (currentId != startId) {
        path.push_back(currentId);
        currentId = parent[currentId];
    }
    path.push_back(startId);
    std::reverse(path.begin(), path.end());
    return path;
}

vector<int> SocialNetwork::findCommonFriends(int id1, int id2) {
    if (!users.count(id1) || !users.count(id2)) {
        cout << "One or more IDs provided does not exist." << endl;
        return {};
    }

    if (id1 == id2) {
        return {};
    }

    vector<int> friendsOfId1 = adjacencyList.at(id1);
    vector<int> friendsOfId2 = adjacencyList.at(id2);
    vector<int> commonFriends;

    // Original algorithm (O(n*m))
    // for (auto& friendId1 : friendsOfId1) {
    //     for (auto& friendId2 : friendsOfId2) {
    //         if (friendId1 == friendId2) {
    //             commonFriends.push_back(friendId1);
    //         }
    //     }
    // }

    // More efficient version using set for O(n + m) time
    set<int> friendsSet(friendsOfId1.begin(), friendsOfId1.end());
    for (auto& friendId2 : friendsOfId2) {
        if (friendsSet.count(friendId2) && friendId2 != id1 && friendId2 != id2) {
            commonFriends.push_back(friendId2);
        }
    }
    return commonFriends;
}

vector<User> SocialNetwork::getAllUsersData() const {
    vector<User> resultVector;

    for (const auto& pair : users) {
        resultVector.push_back(pair.second);
    }

    return resultVector;
}

vector<pair<int, int>> SocialNetwork::getAllConnectionsData() const {
    vector<pair<int, int>> resultVector;

    for (const auto& pair : adjacencyList) {
        int userId = pair.first;
        for (const int& friendId : pair.second) {
            if (userId < friendId) {
                resultVector.emplace_back(userId, friendId);
            }
        }
    }
    return resultVector;
}