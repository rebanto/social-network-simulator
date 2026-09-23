// Social Graph - a Win32 + GDI+ desktop front-end for the SocialNetwork core.
//
// Layout: people sidebar (left), interactive force-directed graph (center),
// profile / analysis panel (right). Everything is custom-painted with GDI+
// into a back buffer; only the "Add person" dialog uses native edit controls.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "SocialNetwork.h"
#include "User.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

namespace theme {
const UINT bg        = 0x0E1016;
const UINT panel     = 0x141720;
const UINT card      = 0x1B1F2A;
const UINT cardHover = 0x242938;
const UINT border    = 0x272C3A;
const UINT text      = 0xE7E9F0;
const UINT muted     = 0x8A91A6;
const UINT faint     = 0x5D6479;
const UINT accent    = 0x6E7BFF;
const UINT accentHi  = 0x8591FF;
const UINT target    = 0xFF6B9A;
const UINT path      = 0xFFB547;
const UINT common    = 0x34D399;
const UINT edge      = 0x343B4E;
const UINT grid      = 0x1B1F2B;
}

const UINT kAvatarColors[] = {
    0x6E7BFF, 0x34D399, 0xF59E0B, 0xFF6B9A, 0x38BDF8,
    0xA78BFA, 0xF472B6, 0x2DD4BF, 0xFB923C, 0x84CC16,
};

static Color Hex(UINT rgb, BYTE a = 255) {
    return Color(a, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}
static COLORREF Ref(UINT rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

template <class T> static T Min(T a, T b) { return a < b ? a : b; }
template <class T> static T Max(T a, T b) { return a > b ? a : b; }
template <class T> static T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct Node {
    float x = 0, y = 0, vx = 0, vy = 0;
    bool fixed = false;
};

enum Action {
    A_NONE = 0,
    A_ADD_USER,
    A_SELECT,
    A_CLEAR_TARGET,
    A_CONNECT,
    A_DISCONNECT,
    A_FIT,
    A_RELAYOUT,
};

struct Hit {
    RectF r;
    int action;
    int param;
};

enum { TIMER_SIM = 1, TIMER_TOAST = 2 };

SocialNetwork g_net;
HWND g_hwnd = nullptr;
HWND g_hDlg = nullptr;
float g_dpi = 1.0f;

std::vector<User> g_users;
std::vector<std::pair<int, int>> g_edges;
std::map<int, std::vector<int>> g_friends;
std::map<int, Node> g_nodes;

float g_alpha = 1.0f;
float g_alphaTarget = 0.0f;
float g_zoom = 1.0f, g_camX = 0.0f, g_camY = 0.0f;
bool g_autoFit = true;

int g_sel = -1, g_target = -1, g_hoverNode = -1;
std::vector<int> g_path, g_common;

RectF g_sidebarRect, g_graphRect, g_rightRect, g_listRect;
float g_listScroll = 0, g_listContentH = 0;
float g_rightScroll = 0, g_rightContentH = 0;

std::vector<Hit> g_hits;
int g_hotAction = A_NONE, g_hotParam = 0;
int g_pressAction = A_NONE, g_pressParam = 0;
bool g_wantHand = false;
bool g_trackingLeave = false;

int g_dragNode = -1;
bool g_panning = false;
bool g_mouseMoved = false;
POINT g_lastMouse = {0, 0}, g_downMouse = {0, 0};

std::wstring g_toast;
ULONGLONG g_toastUntil = 0;

std::map<int, std::unique_ptr<Font>> g_fontCache;
std::unique_ptr<Bitmap> g_gridTile;
std::unique_ptr<TextureBrush> g_gridBrush;

static float S(float v) { return v * g_dpi; }

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::vector<std::string> ParseInterests(const std::wstring& w) {
    std::vector<std::string> out;
    std::stringstream ss(Narrow(w));
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static const User* UserById(int id) {
    for (const auto& u : g_users)
        if (u.id == id) return &u;
    return nullptr;
}

static std::wstring NameOf(int id) {
    const User* u = UserById(id);
    return u ? Widen(u->name) : L"?";
}

static std::wstring FirstName(int id) {
    std::wstring n = NameOf(id);
    size_t sp = n.find(L' ');
    return sp == std::wstring::npos ? n : n.substr(0, sp);
}

static std::wstring Initials(const std::string& name) {
    std::wstring w = Widen(name), out;
    bool start = true;
    for (wchar_t c : w) {
        if (c == L' ') { start = true; continue; }
        if (start && out.size() < 2) out += (wchar_t)towupper(c);
        start = false;
    }
    return out.empty() ? L"?" : out;
}

static UINT AvatarColor(int id) {
    return kAvatarColors[(unsigned)(id - 1) % (sizeof(kAvatarColors) / sizeof(kAvatarColors[0]))];
}

static int Degree(int id) {
    auto it = g_friends.find(id);
    return it == g_friends.end() ? 0 : (int)it->second.size();
}

static bool AreFriends(int a, int b) {
    auto it = g_friends.find(a);
    if (it == g_friends.end()) return false;
    for (int f : it->second)
        if (f == b) return true;
    return false;
}

static bool Contains(const RectF& r, float x, float y) {
    return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height;
}

static RectF IntersectRect(const RectF& a, const RectF& b) {
    float x1 = Max(a.X, b.X), y1 = Max(a.Y, b.Y);
    float x2 = Min(a.X + a.Width, b.X + b.Width), y2 = Min(a.Y + a.Height, b.Y + b.Height);
    return RectF(x1, y1, Max(0.0f, x2 - x1), Max(0.0f, y2 - y1));
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

static Font* GetFont(float px, bool semibold) {
    int key = (int)(px * 4.0f) * 2 + (semibold ? 1 : 0);
    auto& f = g_fontCache[key];
    if (!f) {
        f.reset(new Font(semibold ? L"Segoe UI Semibold" : L"Segoe UI", px, FontStyleRegular, UnitPixel));
        if (f->GetLastStatus() != Ok)
            f.reset(new Font(L"Segoe UI", px, semibold ? FontStyleBold : FontStyleRegular, UnitPixel));
    }
    return f.get();
}

static Font* F(float pt, bool semibold = false) { return GetFont(S(pt), semibold); }

static void AddRound(GraphicsPath& p, const RectF& r, float rad) {
    float d = Min(rad * 2, Min(r.Width, r.Height));
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void FillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    AddRound(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

static void StrokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w, bool dashed = false) {
    GraphicsPath p;
    AddRound(p, r, rad);
    Pen pen(c, w);
    if (dashed) pen.SetDashStyle(DashStyleDash);
    g.DrawPath(&pen, &p);
}

static void FillCircle(Graphics& g, float cx, float cy, float r, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}

static void StrokeCircle(Graphics& g, float cx, float cy, float r, const Color& c, float w) {
    Pen pen(c, w);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);
}

static void DrawText(Graphics& g, const std::wstring& s, Font* f, const RectF& r, const Color& c,
                     StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentCenter) {
    StringFormat sf;
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, r, &sf, &b);
}

// Draws word-wrapped text and returns the height it used.
static float DrawWrapped(Graphics& g, const std::wstring& s, Font* f, float x, float y, float w, const Color& c,
                         StringAlignment h = StringAlignmentNear) {
    StringFormat sf;
    sf.SetAlignment(h);
    RectF bounds;
    g.MeasureString(s.c_str(), -1, f, RectF(x, y, w, 10000.0f), &sf, &bounds);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, RectF(x, y, w, bounds.Height + 2), &sf, &b);
    return bounds.Height;
}

static float MeasureText(Graphics& g, const std::wstring& s, Font* f) {
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    RectF out;
    g.MeasureString(s.c_str(), -1, f, PointF(0, 0), &sf, &out);
    return out.Width;
}

static void DrawAvatar(Graphics& g, int id, float cx, float cy, float r, BYTE alpha = 255) {
    const User* u = UserById(id);
    FillCircle(g, cx, cy, r, Hex(AvatarColor(id), alpha));
    if (u && r > S(6))
        DrawText(g, Initials(u->name), GetFont(r * 0.78f, true), RectF(cx - r, cy - r, r * 2, r * 2 + S(1)),
                 Color(alpha, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
}

// ---------------------------------------------------------------------------
// Hit testing / buttons
// ---------------------------------------------------------------------------

static void AddHit(const RectF& r, int action, int param) {
    if (r.Width > 0 && r.Height > 0) g_hits.push_back({r, action, param});
}

static const Hit* HitAt(float x, float y) {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (Contains(it->r, x, y)) return &*it;
    return nullptr;
}

static bool IsHot(int a, int p) { return g_hotAction == a && g_hotParam == p; }
static bool IsPressed(int a, int p) { return g_pressAction == a && g_pressParam == p && IsHot(a, p); }

enum class Btn { Primary, Secondary, Danger, Ghost };

static void DrawButton(Graphics& g, const RectF& r, const std::wstring& label, int action, int param, Btn style,
                       float fontPt = 13.0f) {
    bool hot = IsHot(action, param), pressed = IsPressed(action, param);
    float rad = S(8);
    Color fg = Hex(theme::text);
    switch (style) {
    case Btn::Primary:
        FillRound(g, r, rad, Hex(pressed ? theme::accent : (hot ? theme::accentHi : theme::accent), pressed ? 200 : 255));
        fg = Color(255, 255, 255, 255);
        break;
    case Btn::Secondary:
        FillRound(g, r, rad, Hex(hot ? theme::cardHover : theme::card));
        StrokeRound(g, r, rad, Hex(theme::border), S(1));
        break;
    case Btn::Danger:
        FillRound(g, r, rad, hot ? Hex(theme::target, 36) : Hex(theme::card));
        StrokeRound(g, r, rad, Hex(theme::target, hot ? 160 : 90), S(1));
        fg = Hex(theme::target);
        break;
    case Btn::Ghost:
        if (hot) FillRound(g, r, rad, Hex(theme::cardHover));
        fg = Hex(hot ? theme::text : theme::muted);
        break;
    }
    DrawText(g, label, F(fontPt, true), r, fg, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(r, action, param);
}

static void SectionLabel(Graphics& g, float x, float y, float w, const std::wstring& s, const std::wstring& right = L"") {
    DrawText(g, s, F(10.5f, true), RectF(x, y, w, S(16)), Hex(theme::faint));
    if (!right.empty())
        DrawText(g, right, F(10.5f, true), RectF(x, y, w, S(16)), Hex(theme::faint), StringAlignmentFar);
}

struct Chip {
    std::wstring text;
    Color bg, fg;
    Color dot;
    bool hasDot;
};

// Lays chips out left-to-right with wrapping; returns the height used.
static float DrawChips(Graphics& g, float x, float y, float w, const std::vector<Chip>& chips,
                       const std::wstring& sep = L"") {
    Font* f = F(12.0f);
    float h = S(26), padX = S(10), gap = S(6), dotW = S(12);
    float sepW = sep.empty() ? 0 : MeasureText(g, sep, f) + S(2);
    float cx = x, cy = y;
    for (size_t i = 0; i < chips.size(); ++i) {
        const Chip& c = chips[i];
        float cw = Min(MeasureText(g, c.text, f) + padX * 2 + (c.hasDot ? dotW : 0), w);
        float need = cw + (i + 1 < chips.size() ? sepW : 0);
        if (cx > x && cx + need > x + w) { cx = x; cy += h + gap; }
        RectF r(cx, cy, cw, h);
        FillRound(g, r, h / 2, c.bg);
        float tx = r.X + padX;
        if (c.hasDot) {
            FillCircle(g, tx + S(3), r.Y + h / 2, S(3.5f), c.dot);
            tx += dotW;
        }
        DrawText(g, c.text, f, RectF(tx - S(2), r.Y, r.X + r.Width - tx + S(4), h), c.fg);
        cx += cw;
        if (!sep.empty() && i + 1 < chips.size()) {
            DrawText(g, sep, f, RectF(cx, cy, sepW + S(2), h), Hex(theme::faint), StringAlignmentCenter);
            cx += sepW;
        }
        cx += gap;
    }
    return chips.empty() ? 0 : (cy - y) + h;
}

// ---------------------------------------------------------------------------
// Graph model / simulation
// ---------------------------------------------------------------------------

static float Scale() { return g_zoom * g_dpi; }

static PointF ToScreen(const Node& n) {
    return PointF(g_graphRect.X + g_graphRect.Width / 2 + (n.x - g_camX) * Scale(),
                  g_graphRect.Y + g_graphRect.Height / 2 + (n.y - g_camY) * Scale());
}

static void ToWorld(float sx, float sy, float& x, float& y) {
    x = (sx - g_graphRect.X - g_graphRect.Width / 2) / Scale() + g_camX;
    y = (sy - g_graphRect.Y - g_graphRect.Height / 2) / Scale() + g_camY;
}

static float ZoomScale() { return Clamp(std::sqrt(g_zoom), 0.7f, 1.4f); }

static float NodeRadius(int id) { return S(14.0f + 3.2f * std::sqrt((float)Degree(id))) * ZoomScale(); }

static void SeedPosition(Node& n, int index) {
    // Phyllotaxis spiral: an even, deterministic starting arrangement.
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    float r = 55.0f * std::sqrt(0.5f + index), a = index * golden;
    n.x = r * std::cos(a);
    n.y = r * std::sin(a);
    n.vx = n.vy = 0;
}

static void UpdateAnalysis() {
    g_path.clear();
    g_common.clear();
    if (g_sel != -1 && !UserById(g_sel)) g_sel = -1;
    if (g_target != -1 && (!UserById(g_target) || g_sel == -1)) g_target = -1;
    if (g_sel != -1 && g_target != -1) {
        g_path = g_net.getShortestPath(g_sel, g_target);
        g_common = g_net.findCommonFriends(g_sel, g_target);
    }
}

static void RefreshData() {
    g_users = g_net.getAllUsersData();
    g_edges = g_net.getAllConnectionsData();
    g_friends.clear();
    for (const auto& u : g_users) g_friends[u.id] = g_net.getFriends(u.id);

    int index = (int)g_nodes.size();
    for (const auto& u : g_users) {
        if (g_nodes.count(u.id)) continue;
        Node n;
        const Node* anchor = nullptr;
        for (int f : g_friends[u.id]) {
            auto it = g_nodes.find(f);
            if (it != g_nodes.end()) { anchor = &it->second; break; }
        }
        if (anchor) {
            float a = u.id * 2.399f;
            n.x = anchor->x + 40.0f * std::cos(a);
            n.y = anchor->y + 40.0f * std::sin(a);
        } else if (!g_nodes.empty()) {
            float a = u.id * 2.399f;
            n.x = g_camX + 60.0f * std::cos(a);
            n.y = g_camY + 60.0f * std::sin(a);
        } else {
            SeedPosition(n, index);
        }
        g_nodes[u.id] = n;
        ++index;
    }
    UpdateAnalysis();
}

static void Reheat(float alpha) {
    g_alpha = Max(g_alpha, alpha);
    if (g_hwnd) SetTimer(g_hwnd, TIMER_SIM, 16, nullptr);
}

static void SimTick() {
    const float linkDistance = 120.0f, linkStrength = 0.35f;
    const float charge = -950.0f, gravity = 0.035f, decay = 0.6f;

    for (const auto& e : g_edges) {
        Node& a = g_nodes[e.first];
        Node& b = g_nodes[e.second];
        float dx = (b.x + b.vx) - (a.x + a.vx), dy = (b.y + b.vy) - (a.y + a.vy);
        float l = Max(std::sqrt(dx * dx + dy * dy), 0.001f);
        float k = (l - linkDistance) / l * g_alpha * linkStrength;
        dx *= k; dy *= k;
        b.vx -= dx * 0.5f; b.vy -= dy * 0.5f;
        a.vx += dx * 0.5f; a.vy += dy * 0.5f;
    }

    std::vector<Node*> ns;
    for (auto& kv : g_nodes) ns.push_back(&kv.second);
    for (size_t i = 0; i < ns.size(); ++i) {
        for (size_t j = i + 1; j < ns.size(); ++j) {
            float dx = ns[j]->x - ns[i]->x, dy = ns[j]->y - ns[i]->y;
            float d2 = Max(dx * dx + dy * dy, 36.0f);
            float f = charge * g_alpha / d2;
            ns[i]->vx += dx * f; ns[i]->vy += dy * f;
            ns[j]->vx -= dx * f; ns[j]->vy -= dy * f;
        }
    }

    for (auto& kv : g_nodes) {
        Node* n = &kv.second;
        if (n->fixed) { n->vx = n->vy = 0; continue; }
        // People with no friends feel a stronger pull so they don't drift to the edges.
        float pull = gravity * (Degree(kv.first) == 0 ? 4.0f : 1.0f) * g_alpha;
        n->vx -= n->x * pull;
        n->vy -= n->y * pull;
        n->vx *= decay; n->vy *= decay;
        n->x += n->vx; n->y += n->vy;
    }

    g_alpha += (g_alphaTarget - g_alpha) * 0.0228f;
}

// Moves the camera toward a view that frames every node. Returns true once settled.
static bool FitView(bool instant) {
    if (g_nodes.empty() || g_graphRect.Width <= 0) return true;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const auto& kv : g_nodes) {
        minx = Min(minx, kv.second.x); maxx = Max(maxx, kv.second.x);
        miny = Min(miny, kv.second.y); maxy = Max(maxy, kv.second.y);
    }
    float pad = S(90);
    float w = Max(maxx - minx, 1.0f), h = Max(maxy - miny, 1.0f);
    float z = Min((g_graphRect.Width - 2 * pad) / w, (g_graphRect.Height - 2 * pad) / h) / g_dpi;
    z = Clamp(z, 0.35f, 1.6f);
    float cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    if (instant) {
        g_zoom = z; g_camX = cx; g_camY = cy;
        return true;
    }
    const float t = 0.14f;
    g_zoom += (z - g_zoom) * t;
    g_camX += (cx - g_camX) * t;
    g_camY += (cy - g_camY) * t;
    return std::fabs(z - g_zoom) < 0.001f && std::fabs(cx - g_camX) < 0.5f && std::fabs(cy - g_camY) < 0.5f;
}

static int NodeAt(float sx, float sy) {
    int best = -1;
    float bestD = 1e9f;
    for (const auto& kv : g_nodes) {
        PointF p = ToScreen(kv.second);
        float d = std::hypot(p.X - sx, p.Y - sy);
        if (d <= NodeRadius(kv.first) + S(4) && d < bestD) { best = kv.first; bestD = d; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Selection / actions
// ---------------------------------------------------------------------------

static void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

static void Toast(const std::wstring& msg) {
    g_toast = msg;
    g_toastUntil = GetTickCount64() + 2600;
    SetTimer(g_hwnd, TIMER_TOAST, 2650, nullptr);
    Invalidate();
}

static void EnsureListVisible(int id) {
    float rowH = S(54);
    for (size_t i = 0; i < g_users.size(); ++i) {
        if (g_users[i].id != id) continue;
        float top = i * rowH;
        if (top < g_listScroll) g_listScroll = top;
        else if (top + rowH > g_listScroll + g_listRect.Height) g_listScroll = top + rowH - g_listRect.Height;
    }
}

static void Select(int id) {
    g_sel = id;
    if (g_target == id) g_target = -1;
    g_rightScroll = 0;
    UpdateAnalysis();
    if (id != -1) EnsureListVisible(id);
    Invalidate();
}

static void SetTarget(int id) {
    if (id != -1 && g_sel == -1) { Select(id); return; }
    if (id == g_sel) return;
    g_target = id;
    UpdateAnalysis();
    Invalidate();
}

static void Relayout() {
    int i = 0;
    for (auto& kv : g_nodes) SeedPosition(kv.second, i++);
    g_alpha = 1.0f;
    g_autoFit = true;
    Reheat(1.0f);
}

void ShowAddUserDialog(HWND owner);

static void DoAction(int action, int param) {
    switch (action) {
    case A_ADD_USER: ShowAddUserDialog(g_hwnd); break;
    case A_SELECT: Select(param); break;
    case A_CLEAR_TARGET: SetTarget(-1); break;
    case A_CONNECT:
        if (g_sel != -1 && g_target != -1 && g_net.addConnection(g_sel, g_target)) {
            RefreshData();
            Reheat(0.5f);
            Toast(FirstName(g_sel) + L" and " + FirstName(g_target) + L" are now friends");
        }
        break;
    case A_DISCONNECT:
        if (g_sel != -1 && g_target != -1 && g_net.removeConnection(g_sel, g_target)) {
            RefreshData();
            Reheat(0.5f);
            Toast(L"Removed the connection between " + FirstName(g_sel) + L" and " + FirstName(g_target));
        }
        break;
    case A_FIT:
        g_autoFit = true;
        SetTimer(g_hwnd, TIMER_SIM, 16, nullptr);
        break;
    case A_RELAYOUT: Relayout(); break;
    }
    Invalidate();
}

// ---------------------------------------------------------------------------
// Painting: sidebar
// ---------------------------------------------------------------------------

static void DrawLogo(Graphics& g, float x, float y, float size) {
    FillRound(g, RectF(x, y, size, size), S(8), Hex(theme::accent, 40));
    PointF a(x + size * 0.30f, y + size * 0.34f), b(x + size * 0.72f, y + size * 0.28f), c(x + size * 0.52f, y + size * 0.74f);
    Pen pen(Hex(theme::accentHi, 200), S(1.6f));
    g.DrawLine(&pen, a, b); g.DrawLine(&pen, b, c); g.DrawLine(&pen, c, a);
    FillCircle(g, a.X, a.Y, S(3.4f), Hex(theme::accentHi));
    FillCircle(g, b.X, b.Y, S(3.4f), Hex(theme::target));
    FillCircle(g, c.X, c.Y, S(3.4f), Hex(theme::common));
}

static void DrawScrollbar(Graphics& g, const RectF& view, float contentH, float scroll) {
    if (contentH <= view.Height + 1) return;
    float trackH = view.Height - S(8);
    float thumbH = Max(S(28), trackH * view.Height / contentH);
    float t = scroll / (contentH - view.Height);
    RectF thumb(view.X + view.Width - S(5), view.Y + S(4) + t * (trackH - thumbH), S(3.5f), thumbH);
    FillRound(g, thumb, S(2), Hex(theme::border, 255));
}

static void PaintSidebar(Graphics& g) {
    const RectF& r = g_sidebarRect;
    SolidBrush panel(Hex(theme::panel));
    g.FillRectangle(&panel, r);
    Pen line(Hex(theme::border), S(1));
    g.DrawLine(&line, r.X + r.Width - S(0.5f), r.Y, r.X + r.Width - S(0.5f), r.Y + r.Height);

    float x = r.X + S(20), w = r.Width - S(40);

    DrawLogo(g, x, S(22), S(32));
    DrawText(g, L"Social Graph", F(16.5f, true), RectF(x + S(44), S(19), w - S(44), S(22)), Hex(theme::text));
    DrawText(g, L"Network visualizer", F(11.5f), RectF(x + S(44), S(39), w - S(44), S(18)), Hex(theme::muted));

    // Stat cards
    int n = (int)g_users.size(), e = (int)g_edges.size();
    wchar_t avg[32];
    swprintf(avg, 32, L"%.1f", n ? (2.0 * e / n) : 0.0);
    struct Stat { std::wstring value, label; UINT color; } stats[] = {
        {std::to_wstring(n), L"People", theme::accentHi},
        {std::to_wstring(e), L"Links", theme::common},
        {avg, L"Avg friends", theme::path},
    };
    float gap = S(8), cw = (w - 2 * gap) / 3, ch = S(62), sy = S(80);
    for (int i = 0; i < 3; ++i) {
        RectF c(x + i * (cw + gap), sy, cw, ch);
        FillRound(g, c, S(10), Hex(theme::card));
        FillRound(g, RectF(c.X + S(12), c.Y + S(12), S(14), S(3)), S(1.5f), Hex(stats[i].color));
        DrawText(g, stats[i].value, F(18.0f, true), RectF(c.X + S(11), c.Y + S(18), c.Width - S(14), S(26)), Hex(theme::text));
        DrawText(g, stats[i].label, F(10.5f), RectF(c.X + S(12), c.Y + S(42), c.Width - S(14), S(15)), Hex(theme::muted));
    }

    DrawButton(g, RectF(x, S(158), w, S(40)), L"+   Add person", A_ADD_USER, 0, Btn::Primary);

    SectionLabel(g, x, S(218), w, L"PEOPLE", std::to_wstring(n));

    g_listRect = RectF(r.X + S(10), S(242), r.Width - S(20), Max(0.0f, r.Height - S(252)));
    float rowH = S(54);
    g_listContentH = rowH * g_users.size();
    g_listScroll = Clamp(g_listScroll, 0.0f, Max(0.0f, g_listContentH - g_listRect.Height));

    g.SetClip(g_listRect);
    for (size_t i = 0; i < g_users.size(); ++i) {
        const User& u = g_users[i];
        RectF row(g_listRect.X, g_listRect.Y + i * rowH - g_listScroll, g_listRect.Width - S(6), rowH - S(4));
        if (row.Y + row.Height < g_listRect.Y || row.Y > g_listRect.Y + g_listRect.Height) continue;

        bool sel = u.id == g_sel, tgt = u.id == g_target, hot = IsHot(A_SELECT, u.id) || u.id == g_hoverNode;
        if (sel) FillRound(g, row, S(10), Hex(theme::accent, 38));
        else if (tgt) FillRound(g, row, S(10), Hex(theme::target, 30));
        else if (hot) FillRound(g, row, S(10), Hex(theme::cardHover));
        if (sel || tgt)
            FillRound(g, RectF(row.X, row.Y + S(12), S(3), row.Height - S(24)), S(1.5f), Hex(sel ? theme::accentHi : theme::target));

        float cy = row.Y + row.Height / 2;
        DrawAvatar(g, u.id, row.X + S(28), cy, S(17));
        float tx = row.X + S(54), tw = row.Width - S(54) - S(44);
        DrawText(g, Widen(u.name), F(13.0f, true), RectF(tx, row.Y + S(7), tw, S(20)), Hex(theme::text));
        DrawText(g, L"@" + Widen(u.username), F(11.5f), RectF(tx, row.Y + S(26), tw, S(18)), Hex(theme::muted));

        RectF badge(row.X + row.Width - S(40), cy - S(11), S(28), S(22));
        FillRound(g, badge, S(11), Hex(sel ? theme::accent : (tgt ? theme::target : theme::card), sel || tgt ? 70 : 255));
        DrawText(g, std::to_wstring(Degree(u.id)), F(11.0f, true), badge, Hex(sel || tgt ? theme::text : theme::muted),
                 StringAlignmentCenter);

        AddHit(IntersectRect(row, g_listRect), A_SELECT, u.id);
    }
    g.ResetClip();
    DrawScrollbar(g, g_listRect, g_listContentH, g_listScroll);
}

// ---------------------------------------------------------------------------
// Painting: graph canvas
// ---------------------------------------------------------------------------

static void EnsureGridBrush() {
    if (g_gridBrush) return;
    int step = Max(8, (int)S(26));
    g_gridTile.reset(new Bitmap(step, step, PixelFormat32bppARGB));
    Graphics tg(g_gridTile.get());
    tg.SetSmoothingMode(SmoothingModeAntiAlias);
    tg.Clear(Hex(theme::bg));
    FillCircle(tg, step / 2.0f, step / 2.0f, Max(1.0f, S(1.1f)), Hex(theme::grid + 0x0A0A0A));
    g_gridBrush.reset(new TextureBrush(g_gridTile.get(), WrapModeTile));
}

static void DrawLegend(Graphics& g) {
    struct Item { UINT color; std::wstring label; bool line; };
    std::vector<Item> items;
    if (g_sel == -1) return;
    items.push_back({theme::accentHi, L"Selected", false});
    if (g_target == -1) {
        items.push_back({theme::accent, L"Friends", true});
    } else {
        items.push_back({theme::target, L"Compared", false});
        if (!g_path.empty()) items.push_back({theme::path, L"Shortest path", true});
        if (!g_common.empty()) items.push_back({theme::common, L"Mutual friends", false});
    }
    Font* f = F(11.5f);
    float x = g_graphRect.X + S(18), y = g_graphRect.Y + S(18), h = S(30);
    float total = S(12);
    for (auto& it : items) total += S(20) + MeasureText(g, it.label, f) + S(18);
    RectF card(x, y, total, h);
    FillRound(g, card, h / 2, Hex(theme::panel, 230));
    StrokeRound(g, card, h / 2, Hex(theme::border), S(1));
    float cx = x + S(14);
    for (auto& it : items) {
        float my = y + h / 2;
        if (it.line) {
            Pen p(Hex(it.color), S(3));
            p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
            g.DrawLine(&p, cx, my, cx + S(12), my);
        } else {
            FillCircle(g, cx + S(6), my, S(5), Hex(it.color));
        }
        cx += S(20);
        float tw = MeasureText(g, it.label, f);
        DrawText(g, it.label, f, RectF(cx, y, tw + S(16), h), Hex(theme::muted));
        cx += tw + S(18);
    }
}

static void PaintGraph(Graphics& g) {
    const RectF& r = g_graphRect;
    EnsureGridBrush();
    g_gridBrush->ResetTransform();
    g_gridBrush->TranslateTransform(std::fmod(r.X + r.Width / 2 - g_camX * Scale(), S(26)),
                                    std::fmod(r.Y + r.Height / 2 - g_camY * Scale(), S(26)));
    g.FillRectangle(g_gridBrush.get(), r);
    g.SetClip(r);

    if (g_users.empty()) {
        DrawText(g, L"No one here yet — add a person to get started.", F(14.0f), r, Hex(theme::muted),
                 StringAlignmentCenter);
        g.ResetClip();
        return;
    }

    bool focus = g_sel != -1;
    std::set<int> pathSet(g_path.begin(), g_path.end());
    std::set<int> commonSet(g_common.begin(), g_common.end());
    std::set<int> friendSet;
    if (focus) {
        auto it = g_friends.find(g_sel);
        if (it != g_friends.end()) friendSet.insert(it->second.begin(), it->second.end());
    }
    std::set<std::pair<int, int>> pathEdges;
    for (size_t i = 0; i + 1 < g_path.size(); ++i)
        pathEdges.insert({Min(g_path[i], g_path[i + 1]), Max(g_path[i], g_path[i + 1])});

    // Edges: regular first, highlighted on top.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& e : g_edges) {
            bool onPath = pathEdges.count({Min(e.first, e.second), Max(e.first, e.second)}) > 0;
            bool incident = focus && (e.first == g_sel || e.second == g_sel);
            bool mutualLink = g_target != -1 &&
                              ((e.first == g_target && commonSet.count(e.second)) ||
                               (e.second == g_target && commonSet.count(e.first)));
            bool highlighted = onPath || incident || mutualLink;
            if ((pass == 1) != highlighted) continue;

            PointF a = ToScreen(g_nodes[e.first]), b = ToScreen(g_nodes[e.second]);
            Color c;
            float w;
            if (onPath)          { c = Hex(theme::path);          w = S(3.2f); }
            else if (incident)   { c = Hex(theme::accentHi, 170); w = S(2.0f); }
            else if (mutualLink) { c = Hex(theme::common, 150);   w = S(1.8f); }
            else                 { c = Hex(theme::edge, focus ? 110 : 255); w = S(1.4f); }
            Pen pen(c, w);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            g.DrawLine(&pen, a, b);
        }
    }

    // Nodes: dimmed/regular first, highlighted on top.
    float zs = ZoomScale();
    Font* label = GetFont(S(11.5f) * zs, false);
    Font* labelBold = GetFont(S(11.5f) * zs, true);
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& u : g_users) {
            int id = u.id;
            bool isSel = id == g_sel, isTgt = id == g_target;
            bool onPath = pathSet.count(id) > 0, isCommon = commonSet.count(id) > 0;
            bool isHover = id == g_hoverNode;
            bool highlighted = isSel || isTgt || onPath || isCommon || isHover;
            if ((pass == 1) != highlighted) continue;

            bool important = !focus || highlighted || friendSet.count(id);
            BYTE a = important ? 255 : 70;
            PointF p = ToScreen(g_nodes[id]);
            float rad = NodeRadius(id);

            if (isSel || isTgt) {
                UINT c = isSel ? theme::accentHi : theme::target;
                FillCircle(g, p.X, p.Y, rad + S(14), Hex(c, 22));
                FillCircle(g, p.X, p.Y, rad + S(8), Hex(c, 40));
                StrokeCircle(g, p.X, p.Y, rad + S(4.5f), Hex(c), S(2.4f));
            } else if (onPath) {
                StrokeCircle(g, p.X, p.Y, rad + S(4), Hex(theme::path), S(2.2f));
            } else if (isCommon) {
                StrokeCircle(g, p.X, p.Y, rad + S(4), Hex(theme::common), S(2.2f));
            }
            if (isHover && !isSel && !isTgt)
                StrokeCircle(g, p.X, p.Y, rad + S(3), Color(110, 255, 255, 255), S(1.5f));

            DrawAvatar(g, id, p.X, p.Y, rad, a);

            if (g_zoom >= 0.4f || highlighted) {
                std::wstring name = Widen(u.name);
                Font* lf = (isSel || isTgt) ? labelBold : label;
                float tw = MeasureText(g, name, lf) + S(14) * zs;
                float th = S(19) * zs;
                RectF pill(p.X - tw / 2, p.Y + rad + S(6), tw, th);
                FillRound(g, pill, th / 2, Hex(theme::bg, (BYTE)(a * 0.78f)));
                DrawText(g, name, lf, pill, Hex(important ? theme::text : theme::muted, a), StringAlignmentCenter);
            }
        }
    }

    DrawLegend(g);

    // Overlay buttons
    float bw = S(92), bh = S(32);
    DrawButton(g, RectF(r.X + r.Width - S(18) - bw, r.Y + S(18), bw, bh), L"Fit view", A_FIT, 0, Btn::Secondary, 12.0f);
    DrawButton(g, RectF(r.X + r.Width - S(26) - bw * 2, r.Y + S(18), bw, bh), L"Re-layout", A_RELAYOUT, 0, Btn::Secondary, 12.0f);

    DrawText(g, L"Click to select  ·  Right-click to compare  ·  Drag to move  ·  Scroll to zoom",
             F(11.5f), RectF(r.X + S(20), r.Y + r.Height - S(36), r.Width - S(40), S(20)), Hex(theme::faint));

    // Toast
    if (!g_toast.empty() && GetTickCount64() < g_toastUntil) {
        Font* f = F(12.5f, true);
        float tw = Min(MeasureText(g, g_toast, f) + S(64), r.Width - S(40));
        RectF t(r.X + (r.Width - tw) / 2, r.Y + r.Height - S(84), tw, S(38));
        FillRound(g, t, S(19), Hex(theme::card));
        StrokeRound(g, t, S(19), Hex(theme::border), S(1));
        FillCircle(g, t.X + S(20), t.Y + t.Height / 2, S(4), Hex(theme::common));
        DrawText(g, g_toast, f, RectF(t.X + S(32), t.Y, t.Width - S(44), t.Height), Hex(theme::text));
    }
    g.ResetClip();
}

// ---------------------------------------------------------------------------
// Painting: profile / analysis panel
// ---------------------------------------------------------------------------

static float PaintEmptyState(Graphics& g, float x, float y, float w) {
    float y0 = y;
    float cx = x + w / 2;
    y += S(28);
    FillCircle(g, cx, y + S(30), S(30), Hex(theme::card));
    DrawLogo(g, cx - S(16), y + S(14), S(32));
    y += S(76);
    DrawText(g, L"No one selected", F(15.5f, true), RectF(x, y, w, S(24)), Hex(theme::text), StringAlignmentCenter);
    y += S(28);
    y += DrawWrapped(g, L"Pick someone from the list or click a node in the graph to see their profile and connections.",
                     F(12.5f), x + S(8), y, w - S(16), Hex(theme::muted), StringAlignmentCenter);
    y += S(26);

    SectionLabel(g, x, y, w, L"SHORTCUTS");
    y += S(24);
    const wchar_t* keys[][2] = {
        {L"Click", L"Select a person"},
        {L"Right-click", L"Compare with selection"},
        {L"Ctrl + Click", L"Compare with selection"},
        {L"Drag", L"Move a node or pan"},
        {L"Scroll", L"Zoom the graph"},
        {L"Esc", L"Clear selection"},
        {L"Ctrl + N", L"Add a person"},
        {L"F", L"Fit graph to view"},
    };
    Font* kf = F(11.0f, true);
    for (auto& k : keys) {
        float kw = MeasureText(g, k[0], kf) + S(16);
        RectF kr(x, y, kw, S(24));
        FillRound(g, kr, S(6), Hex(theme::card));
        StrokeRound(g, kr, S(6), Hex(theme::border), S(1));
        DrawText(g, k[0], kf, kr, Hex(theme::text), StringAlignmentCenter);
        DrawText(g, k[1], F(12.0f), RectF(x + S(104), y, w - S(104), S(24)), Hex(theme::muted));
        y += S(32);
    }
    return y - y0;
}

static float PaintProfile(Graphics& g, float x, float y, float w) {
    float y0 = y;
    const User* u = UserById(g_sel);
    if (!u) return 0;

    // Header
    DrawAvatar(g, u->id, x + S(30), y + S(30), S(30));
    float tx = x + S(76), tw = w - S(76);
    DrawText(g, Widen(u->name), F(17.0f, true), RectF(tx, y + S(2), tw, S(26)), Hex(theme::text));
    DrawText(g, L"@" + Widen(u->username), F(12.5f), RectF(tx, y + S(27), tw, S(18)), Hex(theme::muted));
    int deg = Degree(u->id);
    std::wstring meta = L"Age " + std::to_wstring(u->age) + L"  ·  " + std::to_wstring(deg) +
                        (deg == 1 ? L" friend" : L" friends");
    DrawText(g, meta, F(12.0f), RectF(tx, y + S(45), tw, S(18)), Hex(theme::faint));
    y += S(84);

    // Interests
    SectionLabel(g, x, y, w, L"INTERESTS");
    y += S(24);
    std::vector<Chip> chips;
    for (const auto& i : u->interests)
        if (!i.empty() && i != "None Provided")
            chips.push_back({Widen(i), Hex(theme::card), Hex(theme::text), Color(), false});
    if (chips.empty()) {
        DrawText(g, L"No interests listed", F(12.5f), RectF(x, y, w, S(20)), Hex(theme::muted));
        y += S(20);
    } else {
        y += DrawChips(g, x, y, w, chips);
    }
    y += S(26);

    // Friends
    std::vector<int> friends = g_friends[u->id];
    std::sort(friends.begin(), friends.end(), [](int a, int b) { return NameOf(a) < NameOf(b); });
    SectionLabel(g, x, y, w, L"FRIENDS", std::to_wstring(friends.size()));
    y += S(22);
    if (friends.empty()) {
        y += DrawWrapped(g, L"No friends yet. Right-click someone in the graph, then Connect.", F(12.5f), x, y, w,
                         Hex(theme::muted));
    }
    for (int f : friends) {
        const User* fu = UserById(f);
        if (!fu) continue;
        RectF row(x - S(8), y, w + S(16), S(42));
        bool hot = IsHot(A_SELECT, f), tgt = f == g_target;
        if (tgt) FillRound(g, row, S(8), Hex(theme::target, 28));
        else if (hot) FillRound(g, row, S(8), Hex(theme::cardHover));
        DrawAvatar(g, f, row.X + S(24), row.Y + row.Height / 2, S(14));
        DrawText(g, Widen(fu->name), F(12.5f, true), RectF(row.X + S(46), row.Y, row.Width - S(120), row.Height), Hex(theme::text));
        DrawText(g, L"@" + Widen(fu->username), F(11.5f), RectF(row.X + S(46), row.Y, row.Width - S(58), row.Height),
                 Hex(theme::faint), StringAlignmentFar);
        AddHit(IntersectRect(row, g_rightRect), A_SELECT, f);
        y += S(42);
    }
    y += S(24);

    // Compare
    SectionLabel(g, x, y, w, L"COMPARE");
    y += S(24);
    if (g_target == -1) {
        RectF hint(x, y, w, S(84));
        StrokeRound(g, hint, S(10), Hex(theme::border), S(1.2f), true);
        DrawWrapped(g, L"Right-click another person (or Ctrl+click) to find the shortest path and your mutual friends.",
                    F(12.0f), hint.X + S(16), hint.Y + S(16), hint.Width - S(32), Hex(theme::muted));
        y += hint.Height;
        return y - y0;
    }

    const User* t = UserById(g_target);
    RectF card(x, y, w, S(56));
    FillRound(g, card, S(10), Hex(theme::card));
    FillRound(g, RectF(card.X, card.Y + S(12), S(3), card.Height - S(24)), S(1.5f), Hex(theme::target));
    DrawAvatar(g, g_target, card.X + S(30), card.Y + card.Height / 2, S(16));
    DrawText(g, Widen(t->name), F(13.0f, true), RectF(card.X + S(56), card.Y + S(9), card.Width - S(100), S(20)), Hex(theme::text));
    DrawText(g, L"@" + Widen(t->username), F(11.5f), RectF(card.X + S(56), card.Y + S(28), card.Width - S(100), S(18)), Hex(theme::muted));
    DrawButton(g, RectF(card.X + card.Width - S(42), card.Y + S(12), S(32), S(32)), L"✕", A_CLEAR_TARGET, 0, Btn::Ghost, 12.0f);
    y += card.Height + S(18);

    // Degrees of separation
    if (g_path.empty()) {
        DrawText(g, L"∞", F(26.0f, true), RectF(x, y, S(40), S(36)), Hex(theme::faint));
        DrawText(g, L"Not connected", F(13.0f, true), RectF(x + S(42), y + S(1), w - S(42), S(18)), Hex(theme::text));
        DrawText(g, L"No path exists between them yet", F(11.5f), RectF(x + S(42), y + S(19), w - S(42), S(18)), Hex(theme::muted));
        y += S(48);
    } else {
        int hops = (int)g_path.size() - 1;
        std::wstring desc = hops == 1 ? L"Direct friends" : (hops == 2 ? L"Friends of friends" : std::to_wstring(hops) + L" hops apart");
        DrawText(g, std::to_wstring(hops), F(26.0f, true), RectF(x, y - S(2), S(40), S(40)), Hex(theme::path));
        DrawText(g, desc, F(13.0f, true), RectF(x + S(42), y + S(1), w - S(42), S(18)), Hex(theme::text));
        DrawText(g, hops == 1 ? L"degree of separation" : L"degrees of separation", F(11.5f),
                 RectF(x + S(42), y + S(19), w - S(42), S(18)), Hex(theme::muted));
        y += S(50);

        std::vector<Chip> pc;
        for (size_t i = 0; i < g_path.size(); ++i) {
            int id = g_path[i];
            UINT c = id == g_sel ? theme::accentHi : (id == g_target ? theme::target : theme::path);
            pc.push_back({FirstName(id), Hex(c, 34), Hex(theme::text), Hex(c), true});
        }
        y += DrawChips(g, x, y, w, pc, L"›");
        y += S(22);
    }

    SectionLabel(g, x, y, w, L"MUTUAL FRIENDS", std::to_wstring(g_common.size()));
    y += S(24);
    if (g_common.empty()) {
        DrawText(g, L"None in common", F(12.5f), RectF(x, y, w, S(20)), Hex(theme::muted));
        y += S(20);
    } else {
        std::vector<Chip> cc;
        for (int id : g_common) cc.push_back({NameOf(id), Hex(theme::common, 30), Hex(theme::text), Hex(theme::common), true});
        y += DrawChips(g, x, y, w, cc);
    }
    y += S(24);

    if (AreFriends(g_sel, g_target))
        DrawButton(g, RectF(x, y, w, S(40)), L"Remove connection", A_DISCONNECT, 0, Btn::Danger);
    else
        DrawButton(g, RectF(x, y, w, S(40)), L"Connect " + FirstName(g_sel) + L" & " + FirstName(g_target), A_CONNECT, 0, Btn::Primary);
    y += S(40);
    return y - y0;
}

static void PaintRight(Graphics& g) {
    const RectF& r = g_rightRect;
    SolidBrush panel(Hex(theme::panel));
    g.FillRectangle(&panel, r);
    Pen line(Hex(theme::border), S(1));
    g.DrawLine(&line, r.X + S(0.5f), r.Y, r.X + S(0.5f), r.Y + r.Height);

    g.SetClip(r);
    float x = r.X + S(24), w = r.Width - S(48);
    float top = r.Y + S(26) - g_rightScroll;
    float used = g_sel == -1 ? PaintEmptyState(g, x, top, w) : PaintProfile(g, x, top, w);
    g.ResetClip();

    g_rightContentH = used + S(52);
    float maxScroll = Max(0.0f, g_rightContentH - r.Height);
    if (g_rightScroll > maxScroll) { g_rightScroll = maxScroll; Invalidate(); }
    DrawScrollbar(g, r, g_rightContentH, g_rightScroll);
}

static void PaintAll(Graphics& g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Hex(theme::bg));
    g_hits.clear();
    PaintGraph(g);
    PaintSidebar(g);
    PaintRight(g);
}

static void Layout(int w, int h) {
    float sw = S(288), rw = S(344);
    g_sidebarRect = RectF(0, 0, sw, (float)h);
    g_rightRect = RectF(w - rw, 0, rw, (float)h);
    g_graphRect = RectF(sw, 0, Max(0.0f, w - sw - rw), (float)h);
}

// ---------------------------------------------------------------------------
// Add-person dialog
// ---------------------------------------------------------------------------

const wchar_t DIALOG_CLASS[] = L"SocialGraphAddPersonDialog";

enum { IDC_FIELD_BASE = 1000 };

struct DialogField {
    const wchar_t* label;
    const wchar_t* hint;
    const wchar_t* placeholder;
    bool numeric;
    HWND edit;
    RectF box;
};

static DialogField g_fields[] = {
    {L"Username", L"", L"e.g. ada_l", false, nullptr, RectF()},
    {L"Full name", L"", L"e.g. Ada Lovelace", false, nullptr, RectF()},
    {L"Age", L"", L"e.g. 36", true, nullptr, RectF()},
    {L"Interests", L"Separate with commas", L"math, poetry, engines", false, nullptr, RectF()},
};
static const int kFieldCount = sizeof(g_fields) / sizeof(g_fields[0]);
static std::wstring g_dlgError;
static HFONT g_editFont = nullptr;
static HBRUSH g_editBrush = nullptr;

static std::wstring FieldText(int i) {
    int len = GetWindowTextLengthW(g_fields[i].edit);
    std::wstring s(len, L'\0');
    if (len) GetWindowTextW(g_fields[i].edit, &s[0], len + 1);
    return s;
}

static void CloseDialog(HWND hwnd) {
    EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE);
    DestroyWindow(hwnd);
}

static void SubmitDialog(HWND hwnd) {
    std::string username = Trim(Narrow(FieldText(0)));
    std::string name = Trim(Narrow(FieldText(1)));
    std::string ageStr = Trim(Narrow(FieldText(2)));
    int bad = -1;

    if (username.empty()) { g_dlgError = L"Please enter a username."; bad = 0; }
    else if (username.find(' ') != std::string::npos) { g_dlgError = L"Usernames can't contain spaces."; bad = 0; }
    else {
        for (const auto& u : g_users)
            if (u.username == username) { g_dlgError = L"That username is already taken."; bad = 0; break; }
    }
    if (bad == -1 && name.empty()) { g_dlgError = L"Please enter a name."; bad = 1; }
    int age = 0;
    if (bad == -1) {
        age = ageStr.empty() ? 0 : atoi(ageStr.c_str());
        if (age < 1 || age > 150) { g_dlgError = L"Age should be a number between 1 and 150."; bad = 2; }
    }
    if (bad != -1) {
        SetFocus(g_fields[bad].edit);
        SendMessageW(g_fields[bad].edit, EM_SETSEL, 0, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    int id = g_net.addUser(username, name, age, ParseInterests(FieldText(3)));
    RefreshData();
    Select(id);
    Reheat(0.6f);
    CloseDialog(hwnd);
    Toast(L"Welcome, " + FirstName(id) + L"! Right-click someone to connect them.");
}

static void PaintDialog(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Hex(theme::panel));

    float pad = S(28), w = rc.right - pad * 2;
    DrawText(g, L"Add a person", F(19.0f, true), RectF(pad, S(22), w, S(28)), Hex(theme::text));
    DrawText(g, L"They'll join the graph right away.", F(12.5f), RectF(pad, S(50), w, S(20)), Hex(theme::muted));

    HWND focus = GetFocus();
    for (int i = 0; i < kFieldCount; ++i) {
        const DialogField& f = g_fields[i];
        RectF labelR(f.box.X, f.box.Y - S(22), f.box.Width, S(18));
        DrawText(g, f.label, F(12.0f, true), labelR, Hex(theme::text));
        if (*f.hint) DrawText(g, f.hint, F(11.5f), labelR, Hex(theme::faint), StringAlignmentFar);
        FillRound(g, f.box, S(8), Hex(theme::card));
        bool focused = focus == f.edit;
        StrokeRound(g, f.box, S(8), focused ? Hex(theme::accent) : Hex(theme::border), focused ? S(1.6f) : S(1));
    }
    if (!g_dlgError.empty()) {
        float ey = g_fields[kFieldCount - 1].box.Y + g_fields[kFieldCount - 1].box.Height + S(14);
        FillCircle(g, pad + S(4), ey + S(9), S(3.5f), Hex(theme::target));
        DrawText(g, g_dlgError, F(12.0f), RectF(pad + S(14), ey, w - S(14), S(18)), Hex(theme::target));
    }
}

static void DrawDialogButton(DRAWITEMSTRUCT* d) {
    Graphics g(d->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    RectF r((REAL)d->rcItem.left, (REAL)d->rcItem.top, (REAL)(d->rcItem.right - d->rcItem.left),
            (REAL)(d->rcItem.bottom - d->rcItem.top));
    SolidBrush bg(Hex(theme::panel));
    g.FillRectangle(&bg, r);
    bool primary = d->CtlID == IDOK;
    bool pressed = (d->itemState & ODS_SELECTED) != 0, focused = (d->itemState & ODS_FOCUS) != 0;
    RectF inner(r.X + S(1), r.Y + S(1), r.Width - S(2), r.Height - S(2));
    if (primary) {
        FillRound(g, inner, S(8), Hex(pressed ? theme::accent : theme::accent, pressed ? 200 : 255));
        if (focused) StrokeRound(g, inner, S(8), Hex(theme::accentHi), S(1.5f));
    } else {
        FillRound(g, inner, S(8), Hex(pressed ? theme::cardHover : theme::card));
        StrokeRound(g, inner, S(8), focused ? Hex(theme::muted) : Hex(theme::border), S(1));
    }
    wchar_t text[64];
    GetWindowTextW(d->hwndItem, text, 64);
    DrawText(g, text, F(13.0f, true), r, primary ? Color(255, 255, 255, 255) : Hex(theme::text), StringAlignmentCenter);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        RECT rc;
        GetClientRect(hwnd, &rc);
        float pad = S(28), w = rc.right - pad * 2;
        float y = S(110);

        if (g_editFont) DeleteObject(g_editFont);
        g_editFont = CreateFontW(-(int)S(14.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Segoe UI");
        if (!g_editBrush) g_editBrush = CreateSolidBrush(Ref(theme::card));

        TEXTMETRICW tm;
        HDC dc = GetDC(hwnd);
        HGDIOBJ old = SelectObject(dc, g_editFont);
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
        int editH = tm.tmHeight + (int)S(2);

        for (int i = 0; i < kFieldCount; ++i) {
            DialogField& f = g_fields[i];
            f.box = RectF(pad, y, w, S(42));
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (f.numeric ? ES_NUMBER : 0);
            f.edit = CreateWindowExW(0, L"EDIT", L"", style, (int)(f.box.X + S(12)),
                                     (int)(f.box.Y + (f.box.Height - editH) / 2), (int)(f.box.Width - S(24)), editH,
                                     hwnd, (HMENU)(INT_PTR)(IDC_FIELD_BASE + i), inst, nullptr);
            SendMessageW(f.edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
            SendMessageW(f.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
            SendMessageW(f.edit, EM_SETCUEBANNER, FALSE, (LPARAM)f.placeholder);
            if (f.numeric) SendMessageW(f.edit, EM_SETLIMITTEXT, 3, 0);
            y += S(78);
        }

        float bh = S(40), by = rc.bottom - pad - bh;
        HWND ok = CreateWindowExW(0, L"BUTTON", L"Add person", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  (int)(rc.right - pad - S(132)), (int)by, (int)S(132), (int)bh, hwnd, (HMENU)IDOK, inst, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      (int)(rc.right - pad - S(132) - S(10) - S(96)), (int)by, (int)S(96), (int)bh, hwnd,
                                      (HMENU)IDCANCEL, inst, nullptr);
        (void)ok; (void)cancel;
        g_dlgError.clear();
        SetFocus(g_fields[0].edit);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, Ref(theme::text));
        SetBkColor(dc, Ref(theme::card));
        return (LRESULT)g_editBrush;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == IDOK) { SubmitDialog(hwnd); return 0; }
        if (id == IDCANCEL) { CloseDialog(hwnd); return 0; }
        if (id >= IDC_FIELD_BASE && id < IDC_FIELD_BASE + kFieldCount &&
            (code == EN_SETFOCUS || code == EN_KILLFOCUS))
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DRAWITEM:
        DrawDialogButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintDialog(hwnd, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        CloseDialog(hwnd);
        return 0;
    case WM_DESTROY:
        g_hDlg = nullptr;
        for (auto& f : g_fields) f.edit = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ApplyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = Ref(theme::panel);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
}

void ShowAddUserDialog(HWND owner) {
    if (g_hDlg) { SetForegroundWindow(g_hDlg); return; }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    if (!GetClassInfoExW(inst, DIALOG_CLASS, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = DIALOG_CLASS;
        RegisterClassExW(&wc);
    }

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, exStyle = WS_EX_DLGMODALFRAME;
    RECT r = {0, 0, (LONG)S(440), (LONG)S(520)};
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, (UINT)(g_dpi * 96));
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT pr;
    GetWindowRect(owner, &pr);
    int x = pr.left + (pr.right - pr.left - ww) / 2, y = pr.top + (pr.bottom - pr.top - wh) / 2;

    g_hDlg = CreateWindowExW(exStyle, DIALOG_CLASS, L"Add person", style, x, y, ww, wh, owner, nullptr, inst, nullptr);
    if (!g_hDlg) return;
    ApplyDarkTitleBar(g_hDlg);
    EnableWindow(owner, FALSE);
    ShowWindow(g_hDlg, SW_SHOW);
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------

static void UpdateHover(float x, float y) {
    const Hit* h = HitAt(x, y);
    int ha = h ? h->action : A_NONE, hp = h ? h->param : 0;
    int node = (!h && Contains(g_graphRect, x, y)) ? NodeAt(x, y) : -1;
    if (ha != g_hotAction || hp != g_hotParam || node != g_hoverNode) {
        g_hotAction = ha;
        g_hotParam = hp;
        g_hoverNode = node;
        Invalidate();
    }
    g_wantHand = h != nullptr || node != -1;
}

static void OnDpiChanged(float dpi) {
    g_dpi = dpi;
    g_fontCache.clear();
    g_gridBrush.reset();
    g_gridTile.reset();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        ApplyDarkTitleBar(hwnd);
        SetTimer(hwnd, TIMER_SIM, 16, nullptr);
        return 0;

    case WM_SIZE:
        Layout(LOWORD(lParam), HIWORD(lParam));
        if (g_autoFit) SetTimer(hwnd, TIMER_SIM, 16, nullptr);
        Invalidate();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = (LONG)S(1040);
        mm->ptMinTrackSize.y = (LONG)S(640);
        return 0;
    }

    case WM_DPICHANGED: {
        OnDpiChanged(HIWORD(wParam) / 96.0f);
        RECT* r = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_SIM) {
            bool hot = g_alpha > 0.004f || g_dragNode != -1;
            if (hot) SimTick();
            bool settled = true;
            if (g_autoFit) settled = FitView(false);
            if (!hot && settled) KillTimer(hwnd, TIMER_SIM);
            Invalidate();
        } else if (wParam == TIMER_TOAST) {
            KillTimer(hwnd, TIMER_TOAST);
            g_toast.clear();
            Invalidate();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            Graphics g(mem);
            PaintAll(g);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            LPCWSTR c = IDC_ARROW;
            if (g_panning && g_mouseMoved) c = IDC_SIZEALL;
            else if (g_dragNode != -1 || g_wantHand) c = IDC_HAND;
            SetCursor(LoadCursor(nullptr, c));
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        if (!g_trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_trackingLeave = true;
        }
        if (std::abs(x - g_downMouse.x) + std::abs(y - g_downMouse.y) > S(3)) g_mouseMoved = true;
        if (g_dragNode != -1) {
            Node& n = g_nodes[g_dragNode];
            ToWorld(x, y, n.x, n.y);
            n.fixed = true;
            n.vx = n.vy = 0;
            g_autoFit = false;
            Reheat(0.25f);
        } else if (g_panning) {
            g_camX -= (x - g_lastMouse.x) / Scale();
            g_camY -= (y - g_lastMouse.y) / Scale();
            if (g_mouseMoved) g_autoFit = false;
            Invalidate();
        } else {
            UpdateHover(x, y);
        }
        g_lastMouse = {(LONG)x, (LONG)y};
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingLeave = false;
        if (g_dragNode == -1 && !g_panning) {
            g_hotAction = A_NONE;
            g_hoverNode = -1;
            Invalidate();
        }
        return 0;

    case WM_LBUTTONDOWN: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        SetFocus(hwnd);
        SetCapture(hwnd);
        g_downMouse = g_lastMouse = {(LONG)x, (LONG)y};
        g_mouseMoved = false;
        if (const Hit* h = HitAt(x, y)) {
            g_pressAction = h->action;
            g_pressParam = h->param;
            if ((wParam & MK_CONTROL) && h->action == A_SELECT) {
                SetTarget(h->param);
                g_pressAction = A_NONE;
            }
            Invalidate();
            return 0;
        }
        if (Contains(g_graphRect, x, y)) {
            int node = NodeAt(x, y);
            if (node != -1) {
                if (wParam & MK_CONTROL) SetTarget(node);
                else if (node != g_sel) Select(node);
                g_dragNode = node;
            } else {
                g_panning = true;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        int pa = g_pressAction, pp = g_pressParam;
        bool wasPanning = g_panning;
        g_pressAction = A_NONE;
        if (g_dragNode != -1) {
            g_nodes[g_dragNode].fixed = false;
            g_dragNode = -1;
        }
        g_panning = false;
        ReleaseCapture();
        if (pa != A_NONE) {
            const Hit* h = HitAt(x, y);
            if (h && h->action == pa && h->param == pp) DoAction(pa, pp);
        } else if (wasPanning && !g_mouseMoved) {
            Select(-1);
        }
        UpdateHover(x, y);
        Invalidate();
        return 0;
    }

    case WM_RBUTTONDOWN: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        if (const Hit* h = HitAt(x, y)) {
            if (h->action == A_SELECT) SetTarget(h->param);
        } else if (Contains(g_graphRect, x, y)) {
            int node = NodeAt(x, y);
            if (node != -1) SetTarget(node);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_dragNode != -1) g_nodes[g_dragNode].fixed = false;
        g_dragNode = -1;
        g_panning = false;
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        float x = (float)pt.x, y = (float)pt.y;
        float d = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
        if (Contains(g_listRect, x, y)) {
            g_listScroll = Clamp(g_listScroll - d * S(80), 0.0f, Max(0.0f, g_listContentH - g_listRect.Height));
        } else if (Contains(g_rightRect, x, y)) {
            g_rightScroll = Clamp(g_rightScroll - d * S(80), 0.0f, Max(0.0f, g_rightContentH - g_rightRect.Height));
        } else if (Contains(g_graphRect, x, y)) {
            float wx, wy;
            ToWorld(x, y, wx, wy);
            g_zoom = Clamp(g_zoom * std::pow(1.15f, d), 0.2f, 4.0f);
            g_camX = wx - (x - g_graphRect.X - g_graphRect.Width / 2) / Scale();
            g_camY = wy - (y - g_graphRect.Y - g_graphRect.Height / 2) / Scale();
            g_autoFit = false;
        }
        Invalidate();
        UpdateHover(x, y);
        return 0;
    }

    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (wParam == VK_ESCAPE) {
            if (g_target != -1) SetTarget(-1);
            else Select(-1);
        } else if (ctrl && wParam == 'N') {
            DoAction(A_ADD_USER, 0);
        } else if (!ctrl && wParam == 'F') {
            DoAction(A_FIT, 0);
        } else if (!ctrl && wParam == 'R') {
            DoAction(A_RELAYOUT, 0);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Sample data + entry point
// ---------------------------------------------------------------------------

static void SeedNetwork() {
    int alice = g_net.addUser("alice_j", "Alice Johnson", 26, {"Cooking", "Swimming", "Biking"});
    int bob = g_net.addUser("bob_w", "Bob Wozniak", 29, {"Coding", "Yoga"});
    int charlie = g_net.addUser("charlie_s", "Charlie Smith", 30, {"Biking", "Cooking", "Yodeling"});
    int david = g_net.addUser("dav_d", "David Brown", 40, {"Coding", "Golf"});
    int emily = g_net.addUser("em_s", "Emily Song", 34, {"Painting", "Coding", "Hiking"});
    int frank = g_net.addUser("frank_o", "Frank Ocean", 31, {"Music", "Surfing"});
    int grace = g_net.addUser("grace_p", "Grace Peters", 27, {"Chess", "Reading"});
    int hana = g_net.addUser("hana_k", "Hana Kim", 24, {"Photography", "Travel"});
    int ivan = g_net.addUser("ivan_p", "Ivan Petrov", 38, {"Chess", "Running"});
    int julia = g_net.addUser("julia_m", "Julia Martins", 29, {"Dance", "Travel"});
    int kofi = g_net.addUser("kofi_a", "Kofi Asante", 33, {"Football", "Music"});
    int lena = g_net.addUser("lena_f", "Lena Fischer", 45, {"Gardening", "Reading"});
    int marco = g_net.addUser("marco_r", "Marco Rossi", 36, {"Cooking", "Wine"});
    int nina = g_net.addUser("nina_p", "Nina Patel", 22, {"Gaming", "Anime", "Photography"});
    int omar = g_net.addUser("omar_h", "Omar Haddad", 41, {"Astronomy"});

    const int links[][2] = {
        {alice, bob}, {alice, charlie}, {alice, david}, {bob, charlie}, {bob, emily},
        {charlie, emily}, {david, emily}, {emily, frank}, {frank, grace}, {frank, kofi},
        {frank, julia}, {julia, hana}, {julia, kofi}, {hana, kofi}, {grace, ivan},
        {ivan, lena}, {lena, marco}, {marco, nina}, {ivan, marco}, {marco, alice},
        {nina, hana},
    };
    for (const auto& l : links) g_net.addConnection(l[0], l[1]);
    (void)omar;  // deliberately left unconnected
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartup(&gdiplusToken, &gsi, nullptr);

    SeedNetwork();
    RefreshData();

    g_dpi = GetDpiForSystem() / 96.0f;

    const wchar_t CLASS_NAME[] = L"SocialGraphMainWindow";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Window registration failed.", L"Social Graph", MB_ICONERROR);
        return 1;
    }

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = Min((int)S(1400), (int)(work.right - work.left) - 40);
    int h = Min((int)S(860), (int)(work.bottom - work.top) - 40);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Social Graph", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                                w, h, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Window creation failed.", L"Social Graph", MB_ICONERROR);
        return 1;
    }

    // The window may land on a monitor with a different DPI than the system default.
    float dpi = GetDpiForWindow(hwnd) / 96.0f;
    if (dpi != g_dpi) OnDpiChanged(dpi);
    RECT rc;
    GetClientRect(hwnd, &rc);
    Layout(rc.right, rc.bottom);
    FitView(true);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (g_hDlg && IsDialogMessageW(g_hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_gridBrush.reset();
    g_gridTile.reset();
    g_fontCache.clear();
    if (g_editFont) DeleteObject(g_editFont);
    if (g_editBrush) DeleteObject(g_editBrush);
    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
