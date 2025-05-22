#ifndef SOCIAL_NETWORK_H
#define SOCIAL_NETWORK_H

#include "User.h"
#include <vector>
#include <map>

class SocialNetwork
{
private:
    std::map<int, User> users;
    std::map<int, std::vector<int>> adjacencyList;
    int nextUserId;
public:
    SocialNetwork();

    // user management
    int addUser(const std::string& username, const std::string& name, int age, const std::vector<std::string>& interests = {});
    User* getUser(int id);
    void displayUserProfile(int id);

    // connection management
    bool addConnection(int id1, int id2);
    bool removeConnection(int id1, int id2);
    void displayFriends(int id);

    // graph algorithms
    std::vector<int> getShortestPath(int startId, int endId);
    std::vector<int> findCommonFriends(int id1, int id2);

    // data retrieval
    std::vector<User> getAllUsersData() const;
    std::vector<std::pair<int, int>> getAllConnectionsData() const;
};

#endif