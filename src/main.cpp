#include <iostream>
#include "User.h"
#include "SocialNetwork.h"

using namespace std;

int main() {
    SocialNetwork myNetwork;

    int alice = myNetwork.addUser("alice_x", "Alice Johnson", 26, {"cooking", "swimming", "biking"});
    int bob = myNetwork.addUser("bob", "Bob Wozniak", 29, {"coding", "eating", "yoga"});
    int charlie = myNetwork.addUser("charlie", "Charlie Smith", 30, {"biking", "cooking", "yodeling"});
    int david = myNetwork.addUser("dav_d", "David Brown", 40, {"coding", "golfing", "yodeling"});
    int emily = myNetwork.addUser("em_s", "Emily Song", 40, {"coding", "golfing", "yodeling"});
    int frank = myNetwork.addUser("frank_o", "Frank Ocean", 40, {"coding", "golfing", "yodeling"});
    int grace = myNetwork.addUser("grace_p", "Grace Peters", 40, {"coding", "golfing", "yodeling"});

    // Alice: Bob, Charlie, David
    // Bob: Alice, Charlie, Emily
    // Charlie: Alice, Bob, Emily
    // David: Alice, Emily
    // Emily: Bob, Charlie, David, Frank
    // Frank: Emily, Grace
    // Grace: Frank

    myNetwork.addConnection(alice, bob);
    myNetwork.addConnection(alice, charlie);
    myNetwork.addConnection(alice, david);

    myNetwork.addConnection(bob, charlie);
    myNetwork.addConnection(bob, emily);

    myNetwork.addConnection(charlie, emily);

    myNetwork.addConnection(david, emily);

    myNetwork.addConnection(emily, frank);

    myNetwork.addConnection(frank, grace);

    auto connections = myNetwork.getAllConnectionsData();
    for (const auto& conn : connections) {
        cout << "(" << conn.first << ", " << conn.second << ")" << endl;
    }

    return 0;
}