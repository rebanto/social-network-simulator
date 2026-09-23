#include "ForceLayout.h"

#include <algorithm>
#include <cmath>

void ForceLayout::seed(int index) {
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    float r = 55.0f * std::sqrt(0.5f + index), a = index * golden;
    Body& b = bodies[index];
    b.x = r * std::cos(a);
    b.y = r * std::sin(a);
    b.vx = b.vy = 0;
}

int ForceLayout::build(int begin, int end, float x0, float y0, float size, int depth) {
    Cell cell;
    cell.x0 = x0; cell.y0 = y0; cell.size = size;
    cell.begin = begin; cell.end = end;
    cell.mass = (float)(end - begin);
    float sx = 0, sy = 0;
    for (int i = begin; i < end; ++i) {
        sx += bodies[order_[i]].x;
        sy += bodies[order_[i]].y;
    }
    cell.cx = sx / cell.mass;
    cell.cy = sy / cell.mass;
    cell.leaf = (end - begin) <= 1 || depth >= 20 || size < 0.5f;
    for (int& c : cell.child) c = -1;

    int index = (int)cells_.size();
    cells_.push_back(cell);
    if (cell.leaf) return index;

    float h = size / 2, mx = x0 + h, my = y0 + h;
    auto first = order_.begin() + begin, last = order_.begin() + end;
    auto midY = std::partition(first, last, [&](int b) { return bodies[b].y < my; });
    auto midTop = std::partition(first, midY, [&](int b) { return bodies[b].x < mx; });
    auto midBottom = std::partition(midY, last, [&](int b) { return bodies[b].x < mx; });
    int bounds[5] = {begin, (int)(midTop - order_.begin()), (int)(midY - order_.begin()),
                     (int)(midBottom - order_.begin()), end};
    float ox[4] = {x0, mx, x0, mx}, oy[4] = {y0, y0, my, my};
    for (int q = 0; q < 4; ++q) {
        if (bounds[q + 1] > bounds[q]) {
            int child = build(bounds[q], bounds[q + 1], ox[q], oy[q], h, depth + 1);
            cells_[index].child[q] = child;
        }
    }
    return index;
}

void ForceLayout::applyRepulsion(int i) {
    Body& b = bodies[i];
    const float strength = charge * alpha;
    const float theta2 = theta * theta;
    int stack[256];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Cell& c = cells_[stack[--top]];
        float dx = c.cx - b.x, dy = c.cy - b.y;
        float d2 = dx * dx + dy * dy;
        if (!c.leaf && c.size * c.size < theta2 * d2) {
            float f = strength * c.mass / std::max(d2, 36.0f);
            b.vx += dx * f;
            b.vy += dy * f;
            continue;
        }
        if (c.leaf) {
            for (int k = c.begin; k < c.end; ++k) {
                int j = order_[k];
                if (j == i) continue;
                float ex = bodies[j].x - b.x, ey = bodies[j].y - b.y;
                float e2 = ex * ex + ey * ey;
                if (e2 < 1e-6f) {  // coincident: nudge apart deterministically
                    ex = (float)((i * 7 + j * 13) % 11 - 5) * 0.1f + 0.05f;
                    ey = (float)((i * 3 + j * 5) % 7 - 3) * 0.1f + 0.05f;
                    e2 = ex * ex + ey * ey;
                }
                float f = strength / std::max(e2, 36.0f);
                b.vx += ex * f;
                b.vy += ey * f;
            }
            continue;
        }
        for (int child : c.child) {
            if (child >= 0 && top < 256) stack[top++] = child;
        }
    }
}

// Pushes apart overlapping bodies. A uniform grid with cells as wide as the
// largest diameter means each body only checks its 3x3 neighborhood: O(n).
void ForceLayout::applyCollisions() {
    float maxR = 0;
    for (const Body& b : bodies) maxR = std::max(maxR, b.radius);
    if (maxR <= 0) return;
    const float cell = maxR * 2;
    auto key = [](long long cx, long long cy) { return (cx << 32) ^ (cy & 0xFFFFFFFFll); };

    for (auto& kv : grid_) kv.second.clear();
    for (int i = 0; i < (int)bodies.size(); ++i) {
        const Body& b = bodies[i];
        grid_[key((long long)std::floor(b.x / cell), (long long)std::floor(b.y / cell))].push_back(i);
    }
    for (int i = 0; i < (int)bodies.size(); ++i) {
        Body& a = bodies[i];
        if (a.radius <= 0) continue;
        long long cx = (long long)std::floor(a.x / cell), cy = (long long)std::floor(a.y / cell);
        for (long long gx = cx - 1; gx <= cx + 1; ++gx) {
            for (long long gy = cy - 1; gy <= cy + 1; ++gy) {
                auto it = grid_.find(key(gx, gy));
                if (it == grid_.end()) continue;
                for (int j : it->second) {
                    if (j <= i) continue;
                    Body& b = bodies[j];
                    float minD = a.radius + b.radius;
                    float dx = (a.x + a.vx) - (b.x + b.vx), dy = (a.y + a.vy) - (b.y + b.vy);
                    float d2 = dx * dx + dy * dy;
                    if (d2 >= minD * minD) continue;
                    if (d2 < 1e-6f) {  // coincident: separate deterministically
                        dx = (float)((i * 7 + j * 13) % 11 - 5) * 0.1f + 0.05f;
                        dy = (float)((i * 3 + j * 5) % 7 - 3) * 0.1f + 0.05f;
                        d2 = dx * dx + dy * dy;
                    }
                    float d = std::sqrt(d2);
                    float push = (minD - d) / d * collideStrength;
                    float ra = a.radius * a.radius, rb = b.radius * b.radius;
                    float shareA = rb / (ra + rb);  // smaller bodies move more
                    if (!a.fixed) { a.vx += dx * push * shareA;       a.vy += dy * push * shareA; }
                    if (!b.fixed) { b.vx -= dx * push * (1 - shareA); b.vy -= dy * push * (1 - shareA); }
                }
            }
        }
    }
}

void ForceLayout::tick() {
    const int n = (int)bodies.size();
    if (n == 0) {
        alpha += (alphaTarget - alpha) * alphaDecay;
        return;
    }

    // Links: springs toward linkDistance. Like d3, strength is 1/min(degree) and
    // the correction is split by degree so hubs aren't yanked around by leaves.
    degree_.assign(n, 0);
    for (const auto& l : links) { degree_[l.first]++; degree_[l.second]++; }
    for (const auto& l : links) {
        Body& a = bodies[l.first];
        Body& b = bodies[l.second];
        float dx = (b.x + b.vx) - (a.x + a.vx), dy = (b.y + b.vy) - (a.y + a.vy);
        float len = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
        int da = degree_[l.first], db = degree_[l.second];
        float strength = 1.0f / (float)std::min(da, db);
        float k = (len - linkDistance) / len * alpha * strength;
        dx *= k; dy *= k;
        float biasB = (float)da / (float)(da + db);
        b.vx -= dx * biasB;       b.vy -= dy * biasB;
        a.vx += dx * (1 - biasB); a.vy += dy * (1 - biasB);
    }

    // Repulsion (Barnes-Hut)
    float minX = bodies[0].x, maxX = minX, minY = bodies[0].y, maxY = minY;
    for (const Body& b : bodies) {
        minX = std::min(minX, b.x); maxX = std::max(maxX, b.x);
        minY = std::min(minY, b.y); maxY = std::max(maxY, b.y);
    }
    float size = std::max(maxX - minX, maxY - minY) + 1.0f;
    order_.resize(n);
    for (int i = 0; i < n; ++i) order_[i] = i;
    cells_.clear();
    build(0, n, minX, minY, size, 0);
    for (int i = 0; i < n; ++i) applyRepulsion(i);
    applyCollisions();

    // Gravity + integration
    for (int i = 0; i < n; ++i) {
        Body& b = bodies[i];
        if (b.fixed) { b.vx = b.vy = 0; continue; }
        float g = (degree_[i] == 0 ? isolatedGravity : gravity) * alpha;
        b.vx -= b.x * g;
        b.vy -= b.y * g;
        b.vx *= 1 - velocityDecay;
        b.vy *= 1 - velocityDecay;
        b.x += b.vx;
        b.y += b.vy;
    }

    alpha += (alphaTarget - alpha) * alphaDecay;
}
