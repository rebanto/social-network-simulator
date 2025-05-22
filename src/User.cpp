#include <iostream>
#include "User.h"

using namespace std;

User::User(int id, const std::string& username, const std::string& name, int age, const std::vector<std::string>& interests)
{
    this->id = id;
    this->username = username;
    this->name = name;
    this->age = age;
    this->interests = interests;
}

void User::displayProfile() const
{
    cout << "\nID: " << id
         << "\nUsername: " << username
         << "\nName: " << name
         << "\nAge: " << age
         << "\nInterests: ";
    bool first = true;
    for (const auto &interest : interests)
    {
        if (!first)
            cout << ", ";
        cout << interest;
        first = false;
    }
    cout << endl;
}