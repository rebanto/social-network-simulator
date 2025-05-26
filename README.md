# Social Network Simulator

This repository contains the foundational C++ core logic for a social network simulator. It's built to efficiently manage users, connections, and perform complex graph analysis.

---

## Features

The C++ core logic provides the following robust functionalities:

* **User Management:**
    * Add new users with unique IDs, usernames, names, ages, and interests.
    * Retrieve user profiles by ID.
    * List all users in the network.
* **Connection Management:**
    * Establish and remove bidirectional friendships between users.
    * List friends of a specific user.
* **Network Analysis (Graph Algorithms):**
    * Find the **shortest path** (degrees of separation) between any two users using Breadth-First Search (BFS).
    * Identify **common friends** between any two users.
* **Network Data Retrieval:**
    * Efficiently retrieve all users and all connections in formats suitable for later visualization.
* **Network Statistics:**
    * Calculate overall network statistics like total users, total connections, and average friends per user.

---

## Core Technologies

* **Language:** C++
* **Core Data Structures:** `std::map`, `std::vector`, `std::set`, `std::queue`

---

## Next Steps

This repository currently houses the robust C++ backend logic. The next major phase is to build a **frontend** to provide a graphical user interface (GUI) for interacting with this social network.

I am considering two primary approaches for the frontend:

1.  **C++ Web Server + JavaScript Frontend:** This would involve adapting the `main.cpp` to run a C++ web server (using a library like Crow or a custom Winsock implementation) that exposes RESTful API endpoints. A separate web-based frontend (using HTML, CSS, and JavaScript frameworks like React or Vue, possibly D3.js for visualization) would then consume these APIs.
2.  **C++ Windows Desktop Application:** This involves building a native Windows application using a UI framework (such as Dear ImGui, MFC, or Qt) that directly links to and utilizes the `SocialNetwork` C++ classes. This would result in a standalone `.exe` file.

The choice of frontend will dictate further development.

---

## Contributing

Feel free to open issues or submit pull requests if you would like to add the frontend/application or improvements for the backend logic.
