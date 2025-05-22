#ifndef USER_H
#define USER_H

#include <string>
#include <vector>

class User
{
public:
    int id;
    std::string username;
    std::string name;
    int age;
    std::vector<std::string> interests;

    User(int id, const std::string& username, const std::string& name, int age, const std::vector<std::string>& interests = {"None Provided"});
    void displayProfile() const;
};

#endif