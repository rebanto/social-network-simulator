#ifndef FORCE_LAYOUT_H
#define FORCE_LAYOUT_H

#include <unordered_map>
#include <utility>
#include <vector>

// A d3-style force-directed layout: links act as springs, every body repels
// every other, bodies can't overlap, and a weak gravity keeps the picture
// centered. Repulsion uses a Barnes-Hut quadtree and collisions a uniform grid,
// so each tick costs O(n log n) instead of O(n^2).
class ForceLayout {
public:
    struct Body {
        float x = 0, y = 0, vx = 0, vy = 0;
        bool fixed = false;  // pinned bodies (e.g. being dragged) don't move
        float radius = 0;    // collision radius; bodies are kept at least r1 + r2 apart
    };

    std::vector<Body> bodies;
    std::vector<std::pair<int, int>> links;  // indices into `bodies`

    float alpha = 1.0f;          // "temperature"; forces scale with it and it cools every tick
    float alphaTarget = 0.0f;    // > 0 keeps the simulation warm (while dragging)
    float alphaDecay = 0.0228f;
    float alphaMin = 0.004f;
    float velocityDecay = 0.4f;
    float linkDistance = 110.0f;
    float charge = -900.0f;      // negative = repel
    float gravity = 0.035f;
    float isolatedGravity = 0.14f;  // stronger pull for bodies without links
    float theta = 0.9f;          // Barnes-Hut accuracy (lower = more exact)
    float collideStrength = 0.7f;

    void tick();
    bool active() const { return alpha > alphaMin || alphaTarget > 0.0f; }
    void reheat(float value) { if (alpha < value) alpha = value; }

    // Places body `index` on a phyllotaxis spiral (an even, deterministic start).
    void seed(int index);

private:
    struct Cell {
        float x0, y0, size;
        float cx, cy, mass;
        int child[4];
        int begin, end;  // range in order_ (leaves only)
        bool leaf;
    };
    std::vector<Cell> cells_;
    std::vector<int> order_;
    std::vector<int> degree_;
    std::unordered_map<long long, std::vector<int>> grid_;

    int build(int begin, int end, float x0, float y0, float size, int depth);
    void applyRepulsion(int body);
    void applyCollisions();
};

#endif
