#include <iostream>
#include "User.h"
#include "SocialNetwork.h"

using namespace std;

int main() {
    SocialNetwork myNetwork;

    int id1 = myNetwork.addUser("alice_x", "Alice Johnson", 26, {"cooking", "swimming", "biking"});
    int id2 = myNetwork.addUser("bob", "Bob Wozniak", 29, {"coding", "eating", "yoga"});
    int id3 = myNetwork.addUser("charlie", "Charlie Smith", 30, {"biking", "cooking", "yodeling"});
    int id4 = myNetwork.addUser("dav_d", "David Brown", 40, {"coding", "golfing", "yodeling"});
    int id5 = myNetwork.addUser("em_s", "Emily Song", 40, {"coding", "golfing", "yodeling"});
    int id6 = myNetwork.addUser("frank_o", "Frank Ocean", 40, {"coding", "golfing", "yodeling"});
    int id7 = myNetwork.addUser("grace_p", "Grace Peters", 40, {"coding", "golfing", "yodeling"});

    myNetwork.addConnection(id1, id2);
    myNetwork.addConnection(id1, id3);

    myNetwork.addConnection(id2, id4);

    myNetwork.addConnection(id3, id5);

    myNetwork.addConnection(id4, id6);

    // myNetwork.addConnection(id6, id7);
    
    auto path = myNetwork.getShortestPath(id1, id7);
    for (const auto& userId : path) {
        cout << userId;
        if (&userId != &path.back()) cout << " -> ";
    }
    cout << endl;

    return 0;
}