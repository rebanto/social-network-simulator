#include "Analytics.h"

#include <algorithm>
#include <numeric>

using namespace std;

namespace analytics {

Components connectedComponents(const SocialNetwork& network) {
    Components result;
    int maxId = network.maxUserId();
    result.componentOf.assign(maxId + 1, -1);

    vector<int> stack;
    vector<int> sizes;
    for (int start = 1; start <= maxId; ++start) {
        if (!network.userExists(start) || result.componentOf[start] != -1) {
            continue;
        }
        int component = result.count++;
        int size = 0;
        stack.push_back(start);
        result.componentOf[start] = component;
        while (!stack.empty()) {
            int id = stack.back();
            stack.pop_back();
            size++;
            for (int friendId : network.getFriends(id)) {
                if (result.componentOf[friendId] == -1) {
                    result.componentOf[friendId] = component;
                    stack.push_back(friendId);
                }
            }
        }
        if (size > result.largestSize) {
            result.largestSize = size;
            result.largestComponent = component;
        }
    }
    return result;
}

Communities detectCommunities(const SocialNetwork& network, int maxIterations, uint64_t seed) {
    Communities result;
    int maxId = network.maxUserId();
    vector<int> label(maxId + 1, -1);
    vector<int> order;
    order.reserve(network.userCount());
    for (int id = 1; id <= maxId; ++id) {
        if (network.degree(id) > 0) {
            label[id] = id;
            order.push_back(id);
        }
    }

    uint64_t state = seed ? seed : 1;
    auto next = [&state]() {  // xorshift64
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    vector<int> count(maxId + 1, 0);
    vector<int> touched, best;
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        for (size_t i = order.size(); i > 1; --i) {
            swap(order[i - 1], order[next() % i]);
        }
        size_t changed = 0;
        for (int id : order) {
            touched.clear();
            int top = 0;
            for (int friendId : network.getFriends(id)) {
                int l = label[friendId];
                if (count[l]++ == 0) touched.push_back(l);
                top = max(top, count[l]);
            }
            best.clear();
            for (int l : touched) {
                if (count[l] == top) best.push_back(l);
                count[l] = 0;
            }
            // Keep the current label when it is among the best (helps convergence).
            if (find(best.begin(), best.end(), label[id]) != best.end()) {
                continue;
            }
            label[id] = best[next() % best.size()];
            changed++;
        }
        if (changed <= order.size() / 1000) {
            break;
        }
    }

    // Renumber communities by size, biggest first.
    vector<int> size(maxId + 1, 0);
    for (int id : order) size[label[id]]++;
    vector<int> labels;
    for (int l = 0; l <= maxId; ++l) {
        if (size[l] > 0) labels.push_back(l);
    }
    sort(labels.begin(), labels.end(), [&](int a, int b) { return size[a] != size[b] ? size[a] > size[b] : a < b; });
    vector<int> renumber(maxId + 1, -1);
    for (size_t i = 0; i < labels.size(); ++i) {
        renumber[labels[i]] = (int)i;
        if (size[labels[i]] >= 2) {
            result.count++;
            if (result.largest.size() < 20) result.largest.emplace_back((int)i, size[labels[i]]);
        }
    }

    result.communityOf.assign(maxId + 1, -1);
    for (int id : order) {
        result.communityOf[id] = renumber[label[id]];
    }
    return result;
}

vector<int> mostConnected(const SocialNetwork& network, size_t count) {
    vector<int> ids;
    ids.reserve(network.userCount());
    network.forEachUser([&](const User& u) { ids.push_back(u.id); });
    auto byDegree = [&](int a, int b) {
        int da = network.degree(a), db = network.degree(b);
        return da != db ? da > db : a < b;
    };
    if (ids.size() > count) {
        nth_element(ids.begin(), ids.begin() + count, ids.end(), byDegree);
        ids.resize(count);
    }
    sort(ids.begin(), ids.end(), byDegree);
    return ids;
}

}  // namespace analytics
