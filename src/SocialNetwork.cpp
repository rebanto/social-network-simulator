#include "SocialNetwork.h"
#include <iostream>
#include <algorithm>

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
        cout << "Error retrieving user." << endl;
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
    auto it1 = find(friends1.begin(), friends1.end(), id2);
    if (it1 == friends1.end()) {
        cout << "Users are not friends." << endl;
        return false;
    }

    auto& friendsOfId1 = adjacencyList.at(id1);
    friendsOfId1.erase(remove(friendsOfId1.begin(), friendsOfId1.end(), id2), friendsOfId1.end());

    auto& friendsOfId2 = adjacencyList.at(id2);
    friendsOfId2.erase(remove(friendsOfId2.begin(), friendsOfId2.end(), id1), friendsOfId2.end());

    return true;
}

void SocialNetwork::displayFriends(int id) {
    User* user = getUser(id);
    
    if (user == nullptr) {
        cout << "User does not exist." << endl;
    } else {
        cout << "Friends of " << user->name << ":" << endl;
        
        auto& friendsOfUser = adjacencyList.at(id);
        if (friendsOfUser.empty()) {
            cout << "No friends found." << endl;
        } else {
            for (auto& friendId : friendsOfUser) {
                User* friendUser = getUser(friendId);
                cout << friendUser->name << endl;
            }
        }
    }
}