#include <iostream>
#include "User.h"

int main() {
    User alice(1, "alice_x", "Alice Smith", 30, {"reading", "hiking", "cooking"});
    User bob(2, "bobby_d", "Bob Johnson", 25);

    alice.displayProfile();
    bob.displayProfile();

    return 0;
}