// Social Graph - a Win32 + GDI+ desktop front-end for the SocialNetwork core.
//
// Layout: people sidebar (left), interactive force-directed graph (center),
// profile / insights panel (right). Everything is custom-painted with GDI+ into
// a back buffer; the search box and the "Add person" dialog use native edits.
//
// Scale: the core holds millions of people. Small networks are drawn in full;
// larger ones switch to focused views (the most connected people, search
// results, or the selected person's circle). Loading, saving, generating and
// whole-network analysis run on a background thread.

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
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Analytics.h"
#include "ForceLayout.h"
#include "Generator.h"
#include "NetworkIO.h"
#include "SocialNetwork.h"
#include "User.h"
#include "ui/Draw.h"

#ifdef _MSC_VER  // MSVC can pull in libraries from source; other toolchains link them explicitly
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

using namespace Gdiplus;
using namespace ui;

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------

constexpr int kEveryoneLimit = 1000;       // draw the whole network up to this many people
constexpr int kOverviewCount = 200;        // otherwise: this many most-connected people
constexpr int kFocusFriends = 120;         // friends drawn around a selected person
constexpr int kSearchShown = 200;          // search results drawn in the graph
constexpr int kSyncAnalysisLimit = 20000;  // re-analyze instantly after edits below this size
constexpr int kFriendsListed = 30;         // friends listed in the profile panel
constexpr int kSuggestions = 5;

enum Action {
    A_NONE = 0,
    A_ADD_USER,
    A_SELECT,
    A_CLEAR_TARGET,
    A_CONNECT,
    A_DISCONNECT,
    A_FIT,
    A_RELAYOUT,
    A_OPEN,
    A_SAVE,
    A_GENERATE_MENU,
    A_GENERATE,
    A_MODAL_CLOSE,
    A_ADD_SUGGESTION,
    A_DELETE_USER,
    A_CLEAR_SEARCH,
    A_SORT,
    A_COLOR_MODE,
    A_ANALYZE,
};

enum { TIMER_SIM = 1, TIMER_TOAST, TIMER_SEARCH, TIMER_SPIN, TIMER_CONFIRM };
enum { WM_APP_JOB_DONE = WM_APP + 1 };
enum { IDC_SEARCH = 900 };

struct Preset {
    const wchar_t* title;
    const wchar_t* detail;
    const wchar_t* hint;
    int users;
    double averageFriends;
};

const Preset kPresets[] = {
    {L"Sample network", L"15 hand-picked people", L"instant", 15, 0},
    {L"Small town", L"1,000 people", L"instant", 1000, 12},
    {L"City", L"10,000 people", L"instant", 10000, 14},
    {L"Metropolis", L"100,000 people · ~1.5M friendships", L"~1 second", 100000, 16},
    {L"Country", L"1,000,000 people · ~9M friendships", L"~5 seconds · ~0.5 GB", 1000000, 18},
    {L"Empty network", L"Start from scratch", L"", 0, 0},
};
constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct Insights {
    long long version = -1;
    NetworkStats stats;
    int components = 0, largestComponent = 0;
    int communities = 0;
    std::vector<std::pair<int, int>> largestCommunities;
    std::vector<int> communityOf;
    std::vector<int> mostConnected;
};

struct JobResult {
    std::unique_ptr<SocialNetwork> network;  // replaces the open network when set
    std::unique_ptr<Insights> insights;
    std::wstring filePath;
    bool opened = false;
    bool saved = false;
    std::wstring message;
    std::wstring error;
};

enum class ViewMode { Everyone, Overview, Focus, Search };
enum class ListSort { Joined, Popular };
enum class NodeColoring { People, Communities };

SocialNetwork g_net;
long long g_netVersion = 0;
std::wstring g_filePath;
bool g_dirty = false;
Insights g_insights;

HWND g_hwnd = nullptr, g_hDlg = nullptr, g_hSearch = nullptr;
HFONT g_searchFont = nullptr;
HBRUSH g_cardBrush = nullptr;

// selection
int g_sel = -1, g_target = -1, g_hoverNode = -1;
std::vector<int> g_path, g_common;
std::vector<FriendSuggestion> g_suggestions;
int g_confirmDelete = -1;

// people list
std::vector<int> g_listIds;
std::string g_query;  // lower-cased search text
ListSort g_listSort = ListSort::Joined;

// graph view
ViewMode g_viewMode = ViewMode::Everyone;
std::vector<int> g_viewIds;
std::unordered_map<int, int> g_viewIndex;  // user id -> body index
ForceLayout g_layout;
std::vector<int> g_topCache;
long long g_topVersion = -1;
float g_zoom = 1.0f, g_camX = 0.0f, g_camY = 0.0f;
bool g_autoFit = true;
NodeColoring g_colorMode = NodeColoring::People;

// layout rectangles and scrolling
RectF g_sidebarRect, g_graphRect, g_rightRect, g_listRect, g_searchRect;
float g_listScroll = 0, g_rightScroll = 0, g_rightContentH = 0;

// input
bool g_wantHand = false, g_trackingLeave = false;
int g_dragNode = -1;
bool g_panning = false, g_mouseMoved = false;
POINT g_lastMouse = {0, 0}, g_downMouse = {0, 0};

// feedback
std::wstring g_toast;
bool g_toastError = false;
ULONGLONG g_toastUntil = 0;
bool g_busy = false;
std::wstring g_busyLabel;
bool g_modalGenerate = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && std::tolower((unsigned char)hay[i + j]) == needle[j]) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

static bool Matches(const User& u, const std::string& q) {
    if (ContainsCI(u.name, q) || ContainsCI(u.username, q)) return true;
    for (const auto& i : u.interests)
        if (ContainsCI(i, q)) return true;
    return false;
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

static std::wstring NameOf(int id) {
    const User* u = g_net.getUser(id);
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

static std::wstring Plural(long long n, const wchar_t* one, const wchar_t* many) {
    return FormatCount(n) + L" " + (n == 1 ? one : many);
}

static bool InsightsCurrent() { return g_insights.version == g_netVersion; }

static int CommunityOf(int id) {
    return id >= 0 && id < (int)g_insights.communityOf.size() ? g_insights.communityOf[id] : -1;
}

static UINT NodeColor(int id) {
    if (g_colorMode == NodeColoring::Communities && !g_insights.communityOf.empty()) {
        int c = CommunityOf(id);
        return c < 0 ? theme::unknown : PaletteColor(c);
    }
    return PaletteColor(id - 1);
}

static void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

static void Toast(const std::wstring& msg, bool error = false) {
    g_toast = msg;
    g_toastError = error;
    g_toastUntil = GetTickCount64() + (error ? 4200 : 2800);
    SetTimer(g_hwnd, TIMER_TOAST, error ? 4250 : 2850, nullptr);
    Invalidate();
}

static std::wstring DocumentName() {
    if (g_filePath.empty()) return L"Untitled network";
    return std::filesystem::path(g_filePath).filename().wstring();
}

static void UpdateTitle() {
    std::wstring t = L"Social Graph — " + DocumentName() + (g_dirty ? L" •" : L"");
    SetWindowTextW(g_hwnd, t.c_str());
}

// ---------------------------------------------------------------------------
// Graph view: which people are drawn, and where
// ---------------------------------------------------------------------------

static float Scale() { return g_zoom * dpi; }

static PointF ToScreen(const ForceLayout::Body& b) {
    return PointF(g_graphRect.X + g_graphRect.Width / 2 + (b.x - g_camX) * Scale(),
                  g_graphRect.Y + g_graphRect.Height / 2 + (b.y - g_camY) * Scale());
}

static void ToWorld(float sx, float sy, float& x, float& y) {
    x = (sx - g_graphRect.X - g_graphRect.Width / 2) / Scale() + g_camX;
    y = (sy - g_graphRect.Y - g_graphRect.Height / 2) / Scale() + g_camY;
}

static float ZoomScale() { return Clamp(std::sqrt(g_zoom), 0.7f, 1.4f); }

// Node size in design pixels at zoom 1: grows with friend count, smaller caps in crowded views.
static float BaseRadius(int id) {
    float cap = g_viewIds.size() > 150 ? 24.0f : 36.0f;
    return Min(11.0f + 3.4f * std::log2(1.0f + (float)g_net.degree(id)), cap);
}

// Nodes scale with zoom like everything else, so what you see matches the physics.
static float NodeRadius(int id) { return Max(S(3.5f), S(BaseRadius(id)) * g_zoom); }

// The network's core: start from the most connected person and keep adding whoever
// has the most friends already in the set (ties go to the more popular person).
// Unlike the plain top-N by friend count, this yields a connected, readable slice.
static const std::vector<int>& NetworkCore() {
    if (g_topVersion == g_netVersion) return g_topCache;
    g_topVersion = g_netVersion;
    g_topCache.clear();
    std::vector<int> hubs = analytics::mostConnected(g_net, kOverviewCount);
    std::unordered_set<int> in;
    std::unordered_map<int, int> linksIn;  // frontier: person -> friends already in the core
    size_t nextHub = 0;
    while (g_topCache.size() < (size_t)kOverviewCount) {
        int best = -1, bestLinks = 0, bestDegree = -1;
        for (const auto& kv : linksIn) {
            int d = g_net.degree(kv.first);
            if (kv.second > bestLinks || (kv.second == bestLinks && d > bestDegree)) {
                best = kv.first; bestLinks = kv.second; bestDegree = d;
            }
        }
        if (best == -1) {  // empty frontier: start from the next hub not yet included
            while (nextHub < hubs.size() && in.count(hubs[nextHub])) nextHub++;
            if (nextHub == hubs.size()) break;
            best = hubs[nextHub];
        }
        g_topCache.push_back(best);
        in.insert(best);
        linksIn.erase(best);
        for (int f : g_net.getFriends(best))
            if (!in.count(f)) linksIn[f]++;
    }
    return g_topCache;
}

// Picks the `count` best-connected ids from `ids` (in place), most connected first.
static void KeepMostConnected(std::vector<int>& ids, size_t count) {
    auto byDegree = [](int a, int b) {
        int da = g_net.degree(a), db = g_net.degree(b);
        return da != db ? da > db : a < b;
    };
    if (ids.size() > count) {
        std::nth_element(ids.begin(), ids.begin() + count, ids.end(), byDegree);
        ids.resize(count);
    }
    std::sort(ids.begin(), ids.end(), byDegree);
}

static void StartSim() {
    if (g_hwnd) SetTimer(g_hwnd, TIMER_SIM, 16, nullptr);
}

static void RebuildView() {
    ViewMode mode;
    std::vector<int> ids;
    std::unordered_set<int> seen;
    auto add = [&](int id) {
        if (g_net.userExists(id) && seen.insert(id).second) ids.push_back(id);
    };

    if (g_net.userCount() <= kEveryoneLimit) {
        mode = ViewMode::Everyone;
        g_net.forEachUser([&](const User& u) { ids.push_back(u.id); });
    } else if (g_sel != -1) {
        mode = ViewMode::Focus;
        add(g_sel);
        add(g_target);
        for (int id : g_path) add(id);
        for (int id : g_common) add(id);
        std::vector<int> friends = g_net.getFriends(g_sel);
        KeepMostConnected(friends, kFocusFriends);
        for (int id : friends) add(id);
        for (const auto& s : g_suggestions) add(s.userId);
    } else if (!g_query.empty()) {
        mode = ViewMode::Search;
        std::vector<int> matches = g_listIds;
        KeepMostConnected(matches, kSearchShown);
        for (int id : matches) add(id);
    } else {
        mode = ViewMode::Overview;
        for (int id : NetworkCore()) add(id);
    }

    std::unordered_map<int, int> index;
    index.reserve(ids.size() * 2);
    for (size_t i = 0; i < ids.size(); ++i) index[ids[i]] = (int)i;

    std::vector<ForceLayout::Body> bodies(ids.size());
    std::vector<char> placed(ids.size(), 0);
    size_t kept = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto it = g_viewIndex.find(ids[i]);
        if (it != g_viewIndex.end() && it->second < (int)g_layout.bodies.size()) {
            bodies[i] = g_layout.bodies[it->second];
            bodies[i].fixed = false;
            placed[i] = 1;
            kept++;
        }
    }
    // New people start next to a friend who is already on screen, or on a spiral.
    const float golden = 2.39996f;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (placed[i]) continue;
        const std::vector<int>& friends = g_net.getFriends(ids[i]);
        int anchor = -1, scanned = 0;
        for (int f : friends) {
            if (++scanned > 300) break;
            auto it = index.find(f);
            if (it != index.end() && placed[it->second]) { anchor = it->second; break; }
        }
        float a = ids[i] * golden;
        if (anchor >= 0) {
            bodies[i].x = bodies[anchor].x + 45.0f * std::cos(a);
            bodies[i].y = bodies[anchor].y + 45.0f * std::sin(a);
        } else {
            float r = 55.0f * std::sqrt(0.5f + (float)i);
            float cx = kept ? g_camX : 0.0f, cy = kept ? g_camY : 0.0f;
            bodies[i].x = cx + r * std::cos(i * golden);
            bodies[i].y = cy + r * std::sin(i * golden);
        }
        placed[i] = 1;
    }

    std::vector<std::pair<int, int>> links;
    for (size_t i = 0; i < ids.size(); ++i) {
        for (int f : g_net.getFriends(ids[i])) {
            if (f <= ids[i]) continue;
            auto it = index.find(f);
            if (it != index.end()) links.emplace_back((int)i, it->second);
        }
    }

    bool sameNodes = ids == g_viewIds;
    bool sameLinks = sameNodes && links == g_layout.links;
    if (kept == 0) {
        g_camX = g_camY = 0;
        g_layout.alpha = 1.0f;
    }
    if (mode != g_viewMode || kept < ids.size() / 2) g_autoFit = true;

    g_viewMode = mode;
    g_viewIds = std::move(ids);
    g_viewIndex = std::move(index);
    for (size_t i = 0; i < bodies.size(); ++i) bodies[i].radius = BaseRadius(g_viewIds[i]) + 5.0f;
    g_layout.bodies = std::move(bodies);
    g_layout.links = std::move(links);
    if (!sameLinks) g_layout.reheat(sameNodes ? 0.35f : 0.7f);
    if (g_dragNode != -1 && !g_viewIndex.count(g_dragNode)) g_dragNode = -1;
    StartSim();
}

// Moves the camera toward a view that frames every node. Returns true once settled.
static bool FitView(bool instant) {
    if (g_layout.bodies.empty() || g_graphRect.Width <= 0) return true;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const auto& b : g_layout.bodies) {
        minx = Min(minx, b.x); maxx = Max(maxx, b.x);
        miny = Min(miny, b.y); maxy = Max(maxy, b.y);
    }
    float padX = S(70), padTop = S(110), padBottom = S(80);
    float w = Max(maxx - minx, 1.0f), h = Max(maxy - miny, 1.0f);
    float z = Min((g_graphRect.Width - 2 * padX) / w, (g_graphRect.Height - padTop - padBottom) / h) / dpi;
    z = Clamp(z, 0.15f, 1.6f);
    float cx = (minx + maxx) / 2;
    float cy = (miny + maxy) / 2 - (padTop - padBottom) / 2 / (z * dpi);
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
    for (size_t i = 0; i < g_viewIds.size(); ++i) {
        PointF p = ToScreen(g_layout.bodies[i]);
        float d = std::hypot(p.X - sx, p.Y - sy);
        if (d <= NodeRadius(g_viewIds[i]) + S(4) && d < bestD) {
            best = g_viewIds[i];
            bestD = d;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// People list, analysis and selection
// ---------------------------------------------------------------------------

static void RebuildList() {
    g_listIds.clear();
    g_net.forEachUser([](const User& u) {
        if (g_query.empty() || Matches(u, g_query)) g_listIds.push_back(u.id);
    });
    if (g_listSort == ListSort::Popular) {
        std::stable_sort(g_listIds.begin(), g_listIds.end(),
                         [](int a, int b) { return g_net.degree(a) > g_net.degree(b); });
    }
}

static void EnsureListVisible(int id) {
    float rowH = S(54);
    auto it = std::find(g_listIds.begin(), g_listIds.end(), id);
    if (it == g_listIds.end()) return;
    float top = (float)(it - g_listIds.begin()) * rowH;
    if (top < g_listScroll) g_listScroll = top;
    else if (top + rowH > g_listScroll + g_listRect.Height) g_listScroll = top + rowH - g_listRect.Height;
}

static void UpdateAnalysis() {
    g_path.clear();
    g_common.clear();
    g_suggestions.clear();
    if (g_sel != -1 && !g_net.userExists(g_sel)) g_sel = -1;
    if (g_target != -1 && (!g_net.userExists(g_target) || g_sel == -1)) g_target = -1;
    if (g_sel != -1) g_suggestions = g_net.suggestFriends(g_sel, kSuggestions);
    if (g_sel != -1 && g_target != -1) {
        g_path = g_net.getShortestPath(g_sel, g_target);
        g_common = g_net.findCommonFriends(g_sel, g_target);
    }
}

static Insights ComputeInsights(const SocialNetwork& net) {
    Insights in;
    in.stats = net.getStats();
    analytics::Components comps = analytics::connectedComponents(net);
    in.components = comps.count;
    in.largestComponent = comps.largestSize;
    analytics::Communities comm = analytics::detectCommunities(net);
    in.communities = comm.count;
    in.largestCommunities = comm.largest;
    in.communityOf = std::move(comm.communityOf);
    in.mostConnected = analytics::mostConnected(net, 5);
    return in;
}

static void Select(int id) {
    g_sel = id;
    if (g_target == id) g_target = -1;
    g_rightScroll = 0;
    g_confirmDelete = -1;
    UpdateAnalysis();
    if (id != -1) EnsureListVisible(id);
    RebuildView();
    Invalidate();
}

static void SetTarget(int id) {
    if (id != -1 && g_sel == -1) { Select(id); return; }
    if (id == g_sel) return;
    g_target = id;
    UpdateAnalysis();
    RebuildView();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Background jobs
// ---------------------------------------------------------------------------

static void StartJob(const std::wstring& label, std::function<void(JobResult&)> work) {
    g_busy = true;
    g_busyLabel = label;
    SetTimer(g_hwnd, TIMER_SPIN, 33, nullptr);
    HWND hwnd = g_hwnd;
    std::thread([work, hwnd]() {
        JobResult* result = new JobResult;
        try {
            work(*result);
        } catch (const std::bad_alloc&) {
            result->network.reset();
            result->error = L"Ran out of memory. Try a smaller network.";
        } catch (const std::exception& e) {
            result->error = Widen(e.what());
        }
        PostMessageW(hwnd, WM_APP_JOB_DONE, 0, (LPARAM)result);
    }).detach();
    Invalidate();
}

static bool CheckIdle() {
    if (g_busy) Toast(L"Please wait — " + g_busyLabel);
    return !g_busy;
}

static void StartAnalysisJob() {
    long long version = g_netVersion;
    StartJob(L"Analyzing " + Plural(g_net.userCount(), L"person", L"people") + L"…", [version](JobResult& r) {
        r.insights.reset(new Insights(ComputeInsights(g_net)));
        r.insights->version = version;
    });
}

static void RefreshInsightsAfterChange() {
    if (g_net.userCount() <= kSyncAnalysisLimit) {
        g_insights = ComputeInsights(g_net);
        g_insights.version = g_netVersion;
    }
}

// Call after any edit to the open network.
static void OnNetworkEdited() {
    g_netVersion++;
    g_dirty = true;
    UpdateTitle();
    RebuildList();
    UpdateAnalysis();
    RefreshInsightsAfterChange();
    RebuildView();
    Invalidate();
}

// Call after the whole network was replaced (opened, generated).
static void OnNetworkReplaced() {
    g_netVersion++;
    g_sel = g_target = g_hoverNode = g_dragNode = -1;
    g_confirmDelete = -1;
    g_query.clear();
    if (g_hSearch) SetWindowTextW(g_hSearch, L"");
    g_listScroll = g_rightScroll = 0;
    g_viewIds.clear();
    g_viewIndex.clear();
    g_layout.bodies.clear();
    g_layout.links.clear();
    g_insights = Insights();
    UpdateAnalysis();
    RebuildList();
    RebuildView();
    g_autoFit = true;
    UpdateTitle();
    if (g_net.userCount() <= kSyncAnalysisLimit) RefreshInsightsAfterChange();
    else StartAnalysisJob();
    Invalidate();
}

static void OnJobDone(JobResult* raw) {
    std::unique_ptr<JobResult> r(raw);
    g_busy = false;
    KillTimer(g_hwnd, TIMER_SPIN);

    if (!r->error.empty()) Toast(r->error, true);
    if (r->saved) {
        g_filePath = r->filePath;
        g_dirty = false;
    }
    if (r->insights && r->insights->version == g_netVersion) g_insights = std::move(*r->insights);
    if (!r->message.empty()) Toast(r->message);
    if (r->network) {
        g_net = std::move(*r->network);
        g_filePath = r->opened ? r->filePath : L"";
        g_dirty = false;
        OnNetworkReplaced();
    }
    UpdateTitle();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

static bool PickFile(bool save, std::wstring& path) {
    wchar_t buffer[4096] = L"";
    if (save) wcsncpy(buffer, g_filePath.empty() ? L"network.sgraph" : g_filePath.c_str(), 4095);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Social Graph network (*.sgraph)\0*.sgraph\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = 4096;
    ofn.lpstrDefExt = L"sgraph";
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buffer;
    return true;
}

static void SaveAsync(bool saveAs) {
    if (!CheckIdle()) return;
    std::wstring path = g_filePath;
    if (saveAs || path.empty()) {
        if (!PickFile(true, path)) return;
    }
    std::wstring name = std::filesystem::path(path).filename().wstring();
    StartJob(L"Saving " + name + L"…", [path, name](JobResult& r) {
        netio::Result res = netio::save(g_net, path);
        if (!res.ok) {
            r.error = L"Couldn't save: " + Widen(res.error);
            return;
        }
        r.saved = true;
        r.filePath = path;
        r.message = L"Saved " + name;
    });
}

// Asks about unsaved changes. Returns true if it's OK to discard the network.
static bool ConfirmDiscard() {
    if (!g_dirty) return true;
    std::wstring msg = L"Save changes to " + DocumentName() + L"?";
    int choice = MessageBoxW(g_hwnd, msg.c_str(), L"Social Graph", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) return true;
    std::wstring path = g_filePath;
    if (path.empty() && !PickFile(true, path)) return false;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    netio::Result res = netio::save(g_net, path);
    if (!res.ok) {
        MessageBoxW(g_hwnd, (L"Couldn't save: " + Widen(res.error)).c_str(), L"Social Graph", MB_ICONERROR);
        return false;
    }
    g_filePath = path;
    g_dirty = false;
    return true;
}

// Loads a network file on the background thread; it replaces the open network when done.
static void LoadAsync(const std::wstring& path) {
    std::wstring name = std::filesystem::path(path).filename().wstring();
    StartJob(L"Opening " + name + L"…", [path, name](JobResult& r) {
        std::unique_ptr<SocialNetwork> net(new SocialNetwork);
        netio::Result res = netio::load(*net, path);
        if (!res.ok) {
            r.error = L"Couldn't open " + name + L": " + Widen(res.error);
            return;
        }
        r.message = L"Opened " + name + L" · " + Plural(net->userCount(), L"person", L"people");
        r.network = std::move(net);
        r.filePath = path;
        r.opened = true;
    });
}

static void OpenAsync() {
    if (!CheckIdle() || !ConfirmDiscard()) return;
    std::wstring path;
    if (PickFile(false, path)) LoadAsync(path);
}

static void SeedSample(SocialNetwork& net) {
    int alice = net.addUser("alice_j", "Alice Johnson", 26, {"Cooking", "Swimming", "Biking"});
    int bob = net.addUser("bob_w", "Bob Wozniak", 29, {"Coding", "Yoga"});
    int charlie = net.addUser("charlie_s", "Charlie Smith", 30, {"Biking", "Cooking", "Yodeling"});
    int david = net.addUser("dav_d", "David Brown", 40, {"Coding", "Golf"});
    int emily = net.addUser("em_s", "Emily Song", 34, {"Painting", "Coding", "Hiking"});
    int frank = net.addUser("frank_o", "Frank Ocean", 31, {"Music", "Surfing"});
    int grace = net.addUser("grace_p", "Grace Peters", 27, {"Chess", "Reading"});
    int hana = net.addUser("hana_k", "Hana Kim", 24, {"Photography", "Travel"});
    int ivan = net.addUser("ivan_p", "Ivan Petrov", 38, {"Chess", "Running"});
    int julia = net.addUser("julia_m", "Julia Martins", 29, {"Dance", "Travel"});
    int kofi = net.addUser("kofi_a", "Kofi Asante", 33, {"Football", "Music"});
    int lena = net.addUser("lena_f", "Lena Fischer", 45, {"Gardening", "Reading"});
    int marco = net.addUser("marco_r", "Marco Rossi", 36, {"Cooking", "Wine"});
    int nina = net.addUser("nina_p", "Nina Patel", 22, {"Gaming", "Anime", "Photography"});
    net.addUser("omar_h", "Omar Haddad", 41, {"Astronomy", "Reading"});  // deliberately unconnected

    const int links[][2] = {
        {alice, bob}, {alice, charlie}, {alice, david}, {bob, charlie}, {bob, emily},
        {charlie, emily}, {david, emily}, {emily, frank}, {frank, grace}, {frank, kofi},
        {frank, julia}, {julia, hana}, {julia, kofi}, {hana, kofi}, {grace, ivan},
        {ivan, lena}, {lena, marco}, {marco, nina}, {ivan, marco}, {marco, alice},
        {nina, hana},
    };
    for (const auto& l : links) net.addConnection(l[0], l[1]);
}

static void ShowGenerateSheet(bool show) {
    g_modalGenerate = show;
    // The native search box would draw on top of the sheet's dimmed backdrop.
    if (g_hSearch) ShowWindow(g_hSearch, show ? SW_HIDE : SW_SHOW);
    if (show) SetFocus(g_hwnd);
}

static void Generate(int preset) {
    if (preset < 0 || preset >= kPresetCount || !CheckIdle()) return;
    ShowGenerateSheet(false);
    if (!ConfirmDiscard()) return;
    const Preset p = kPresets[preset];
    std::wstring label = p.users > 0 ? L"Generating " + Plural(p.users, L"person", L"people") + L"…"
                                     : L"Preparing…";
    StartJob(label, [preset, p](JobResult& r) {
        std::unique_ptr<SocialNetwork> net(new SocialNetwork);
        if (preset == 0) SeedSample(*net);
        else if (p.users > 0) gen::generate(*net, {p.users, p.averageFriends, 2026u + (unsigned)preset});
        r.message = p.users > 0 ? std::wstring(p.title) + L" · " + Plural(net->userCount(), L"person", L"people") +
                                      L", " + Plural(net->connectionCount(), L"friendship", L"friendships")
                                : L"Started an empty network";
        r.network = std::move(net);
    });
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void ShowAddUserDialog(HWND owner);

static void Relayout() {
    for (size_t i = 0; i < g_layout.bodies.size(); ++i) g_layout.seed((int)i);
    g_layout.alpha = 1.0f;
    g_autoFit = true;
    StartSim();
}

static void Connect(int a, int b) {
    if (!CheckIdle()) return;
    NetResult r = g_net.addConnection(a, b);
    if (r != NetResult::Ok) { Toast(Widen(describe(r)), true); return; }
    OnNetworkEdited();
    Toast(FirstName(a) + L" and " + FirstName(b) + L" are now friends");
}

static void DeleteSelected() {
    if (g_sel == -1 || !CheckIdle()) return;
    if (g_confirmDelete != g_sel) {
        g_confirmDelete = g_sel;
        SetTimer(g_hwnd, TIMER_CONFIRM, 4000, nullptr);
        Toast(L"Press Delete again (or click the button) to delete " + FirstName(g_sel));
        return;
    }
    std::wstring name = NameOf(g_sel);
    g_net.removeUser(g_sel);
    g_confirmDelete = -1;
    g_sel = g_target = -1;
    OnNetworkEdited();
    Toast(L"Deleted " + name);
}

static void DoAction(int action, int param) {
    switch (action) {
    case A_ADD_USER: if (CheckIdle()) ShowAddUserDialog(g_hwnd); break;
    case A_SELECT: Select(param); break;
    case A_CLEAR_TARGET: SetTarget(-1); break;
    case A_CONNECT: if (g_sel != -1 && g_target != -1) Connect(g_sel, g_target); break;
    case A_DISCONNECT:
        if (g_sel != -1 && g_target != -1 && CheckIdle()) {
            NetResult r = g_net.removeConnection(g_sel, g_target);
            if (r != NetResult::Ok) { Toast(Widen(describe(r)), true); break; }
            OnNetworkEdited();
            Toast(L"Removed the connection between " + FirstName(g_sel) + L" and " + FirstName(g_target));
        }
        break;
    case A_ADD_SUGGESTION: if (g_sel != -1) Connect(g_sel, param); break;
    case A_DELETE_USER: DeleteSelected(); break;
    case A_FIT: g_autoFit = true; StartSim(); break;
    case A_RELAYOUT: Relayout(); break;
    case A_OPEN: OpenAsync(); break;
    case A_SAVE: SaveAsync(false); break;
    case A_GENERATE_MENU: if (CheckIdle()) ShowGenerateSheet(true); break;
    case A_GENERATE: Generate(param); break;
    case A_MODAL_CLOSE: ShowGenerateSheet(false); break;
    case A_CLEAR_SEARCH:
        SetWindowTextW(g_hSearch, L"");
        SetFocus(g_hwnd);
        break;
    case A_SORT:
        g_listSort = (ListSort)param;
        g_listScroll = 0;
        RebuildList();
        break;
    case A_COLOR_MODE: g_colorMode = (NodeColoring)param; break;
    case A_ANALYZE: if (CheckIdle()) StartAnalysisJob(); break;
    }
    Invalidate();
}

static void ApplySearch() {
    KillTimer(g_hwnd, TIMER_SEARCH);
    int len = GetWindowTextLengthW(g_hSearch);
    std::wstring text(len, L'\0');
    if (len) GetWindowTextW(g_hSearch, &text[0], len + 1);
    std::string q = Lower(Trim(Narrow(text)));
    if (!q.empty() && q[0] == '@') q.erase(0, 1);
    if (q == g_query) return;
    g_query = q;
    g_listScroll = 0;
    RebuildList();
    if (g_viewMode != ViewMode::Everyone && g_sel == -1) RebuildView();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Painting: sidebar
// ---------------------------------------------------------------------------

static void DrawAvatar(Graphics& g, int id, float cx, float cy, float r, BYTE alpha = 255, bool initials = true) {
    FillCircle(g, cx, cy, r, Hex(NodeColor(id), alpha));
    const User* u = g_net.getUser(id);
    if (u && initials && r > S(7))
        Text(g, Initials(u->name), GetFont(std::round(r * 0.78f * 2) / 2, true), RectF(cx - r, cy - r, r * 2, r * 2 + S(1)),
             Color(alpha, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
}

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
    FillRound(g, thumb, S(2), Hex(theme::border));
}

static void DrawSearchIcon(Graphics& g, float cx, float cy, const Color& c) {
    StrokeCircle(g, cx - S(1.5f), cy - S(1.5f), S(5), c, S(1.6f));
    Pen pen(c, S(1.8f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, cx + S(2.3f), cy + S(2.3f), cx + S(6), cy + S(6));
}

static void PaintSidebar(Graphics& g) {
    const RectF& r = g_sidebarRect;
    SolidBrush panel(Hex(theme::panel));
    g.FillRectangle(&panel, r);
    Pen line(Hex(theme::border), S(1));
    g.DrawLine(&line, r.X + r.Width - S(0.5f), r.Y, r.X + r.Width - S(0.5f), r.Y + r.Height);

    float x = r.X + S(20), w = r.Width - S(40);

    DrawLogo(g, x, S(20), S(32));
    Text(g, L"Social Graph", F(16.5f, true), RectF(x + S(44), S(17), w - S(44), S(22)), Hex(theme::text));
    Text(g, DocumentName() + (g_dirty ? L"  •  edited" : L""), F(11.5f), RectF(x + S(44), S(37), w - S(44), S(18)),
         Hex(g_dirty ? theme::path : theme::muted));

    // File toolbar
    float gap = S(8), bw = (w - 2 * gap) / 3;
    Button(g, RectF(x, S(68), bw, S(32)), L"Open", A_OPEN, 0, Btn::Secondary, 12.0f);
    Button(g, RectF(x + bw + gap, S(68), bw, S(32)), L"Save", A_SAVE, 0, Btn::Secondary, 12.0f);
    Button(g, RectF(x + 2 * (bw + gap), S(68), bw, S(32)), L"Generate", A_GENERATE_MENU, 0, Btn::Secondary, 12.0f);

    // Stat cards
    long long n = g_net.userCount(), e = g_net.connectionCount();
    wchar_t avg[32];
    swprintf(avg, 32, L"%.1f", n ? (2.0 * e / n) : 0.0);
    struct Stat { std::wstring value, label; UINT color; } stats[] = {
        {CompactCount(n), L"People", theme::accentHi},
        {CompactCount(e), L"Friendships", theme::common},
        {avg, L"Avg friends", theme::path},
    };
    float cw = (w - 2 * gap) / 3, ch = S(62), sy = S(112);
    for (int i = 0; i < 3; ++i) {
        RectF c(x + i * (cw + gap), sy, cw, ch);
        FillRound(g, c, S(10), Hex(theme::card));
        FillRound(g, RectF(c.X + S(12), c.Y + S(12), S(14), S(3)), S(1.5f), Hex(stats[i].color));
        Text(g, stats[i].value, F(18.0f, true), RectF(c.X + S(11), c.Y + S(18), c.Width - S(14), S(26)), Hex(theme::text));
        Text(g, stats[i].label, F(10.5f), RectF(c.X + S(12), c.Y + S(42), c.Width - S(14), S(15)), Hex(theme::muted));
    }

    Button(g, RectF(x, S(186), w, S(40)), L"+   Add person", A_ADD_USER, 0, Btn::Primary);

    // Search box (the native edit control sits inside this frame)
    g_searchRect = RectF(x, S(238), w, S(38));
    bool focused = GetFocus() == g_hSearch;
    FillRound(g, g_searchRect, S(8), Hex(theme::card));
    StrokeRound(g, g_searchRect, S(8), focused ? Hex(theme::accent) : Hex(theme::border), focused ? S(1.5f) : S(1));
    DrawSearchIcon(g, g_searchRect.X + S(18), g_searchRect.Y + g_searchRect.Height / 2, Hex(theme::muted));
    if (GetWindowTextLengthW(g_hSearch) > 0) {
        RectF clear(g_searchRect.X + g_searchRect.Width - S(32), g_searchRect.Y + S(5), S(28), S(28));
        Button(g, clear, L"✕", A_CLEAR_SEARCH, 0, Btn::Ghost, 11.0f);
    } else if (!focused) {
        Text(g, L"Ctrl+F", F(10.5f), RectF(g_searchRect.X, g_searchRect.Y, g_searchRect.Width - S(12), g_searchRect.Height),
             Hex(theme::faint), StringAlignmentFar);
    }

    // List header: count + sort toggle
    float hy = S(292);
    std::wstring count = g_query.empty() ? FormatCount(n)
                                         : FormatCount((long long)g_listIds.size()) + L" of " + FormatCount(n);
    SectionLabel(g, x, hy, w, L"PEOPLE  ·  " + count);
    Font* sf = F(11.0f, true);
    const wchar_t* sorts[] = {L"Joined", L"Popular"};
    float sx = x + w;
    for (int i = 1; i >= 0; --i) {
        float tw = MeasureText(g, sorts[i], sf) + S(16);
        sx -= tw;
        RectF sr(sx, hy - S(4), tw, S(24));
        bool active = (int)g_listSort == i;
        if (active) FillRound(g, sr, S(12), Hex(theme::card));
        else if (IsHot(A_SORT, i)) FillRound(g, sr, S(12), Hex(theme::cardHover));
        Text(g, sorts[i], sf, sr, Hex(active ? theme::text : theme::faint), StringAlignmentCenter);
        AddHit(sr, A_SORT, i);
        sx -= S(2);
    }

    // Virtualized list: only rows in view are drawn.
    g_listRect = RectF(r.X + S(10), S(318), r.Width - S(20), Max(0.0f, r.Height - S(328)));
    float rowH = S(54);
    float contentH = rowH * g_listIds.size();
    g_listScroll = Clamp(g_listScroll, 0.0f, Max(0.0f, contentH - g_listRect.Height));

    if (g_listIds.empty()) {
        std::wstring msg = g_query.empty() ? L"No people yet. Add someone or generate a network." : L"No one matches your search.";
        WrappedText(g, msg, F(12.5f), g_listRect.X + S(10), g_listRect.Y + S(12), g_listRect.Width - S(20), Hex(theme::muted));
    }

    g.SetClip(g_listRect);
    size_t first = (size_t)(g_listScroll / rowH);
    size_t last = Min(g_listIds.size(), (size_t)((g_listScroll + g_listRect.Height) / rowH) + 1);
    for (size_t i = first; i < last; ++i) {
        const User* u = g_net.getUser(g_listIds[i]);
        if (!u) continue;
        RectF row(g_listRect.X, g_listRect.Y + i * rowH - g_listScroll, g_listRect.Width - S(6), rowH - S(4));

        bool sel = u->id == g_sel, tgt = u->id == g_target, hot = IsHot(A_SELECT, u->id) || u->id == g_hoverNode;
        if (sel) FillRound(g, row, S(10), Hex(theme::accent, 38));
        else if (tgt) FillRound(g, row, S(10), Hex(theme::target, 30));
        else if (hot) FillRound(g, row, S(10), Hex(theme::cardHover));
        if (sel || tgt)
            FillRound(g, RectF(row.X, row.Y + S(12), S(3), row.Height - S(24)), S(1.5f), Hex(sel ? theme::accentHi : theme::target));

        float cy = row.Y + row.Height / 2;
        DrawAvatar(g, u->id, row.X + S(28), cy, S(17));
        float tx = row.X + S(54), tw = row.Width - S(54) - S(52);
        Text(g, Widen(u->name), F(13.0f, true), RectF(tx, row.Y + S(7), tw, S(20)), Hex(theme::text));
        Text(g, L"@" + Widen(u->username), F(11.5f), RectF(tx, row.Y + S(26), tw, S(18)), Hex(theme::muted));

        std::wstring deg = CompactCount(g_net.degree(u->id));
        float bw2 = Max(S(28), MeasureText(g, deg, F(11.0f, true)) + S(14));
        RectF badge(row.X + row.Width - S(12) - bw2, cy - S(11), bw2, S(22));
        FillRound(g, badge, S(11), Hex(sel ? theme::accent : (tgt ? theme::target : theme::card), sel || tgt ? 70 : 255));
        Text(g, deg, F(11.0f, true), badge, Hex(sel || tgt ? theme::text : theme::muted), StringAlignmentCenter);

        AddHit(Intersect(row, g_listRect), A_SELECT, u->id);
    }
    g.ResetClip();
    DrawScrollbar(g, g_listRect, contentH, g_listScroll);
}

// ---------------------------------------------------------------------------
// Painting: graph canvas
// ---------------------------------------------------------------------------

static void DrawGrid(Graphics& g, const RectF& r) {
    SolidBrush bg(Hex(theme::bg));
    g.FillRectangle(&bg, r);
    float step = S(26);
    if (step < 6) return;
    float ox = std::fmod(r.X + r.Width / 2 - g_camX * Scale(), step);
    float oy = std::fmod(r.Y + r.Height / 2 - g_camY * Scale(), step);
    if (ox < 0) ox += step;
    if (oy < 0) oy += step;
    SolidBrush dot(Hex(theme::grid));
    float d = Max(1.5f, S(1.6f));
    for (float y = r.Y + oy; y < r.Y + r.Height; y += step)
        for (float x = r.X + ox; x < r.X + r.Width; x += step) g.FillRectangle(&dot, x - d / 2, y - d / 2, d, d);
}

static std::wstring ViewCaption() {
    long long n = g_net.userCount();
    switch (g_viewMode) {
    case ViewMode::Everyone: return L"Everyone · " + Plural(n, L"person", L"people");
    case ViewMode::Overview:
        return L"Network core · " + FormatCount((long long)g_viewIds.size()) + L" of " + FormatCount(n) + L" people";
    case ViewMode::Search: {
        long long total = (long long)g_listIds.size();
        if (total <= kSearchShown) return L"Search results · " + Plural(total, L"match", L"matches");
        return L"Top " + FormatCount(kSearchShown) + L" of " + FormatCount(total) + L" matches";
    }
    case ViewMode::Focus: {
        long long friends = g_net.degree(g_sel);
        std::wstring s = FirstName(g_sel) + L"’s circle · ";
        if (friends > kFocusFriends) return s + FormatCount(kFocusFriends) + L" of " + FormatCount(friends) + L" friends shown";
        return s + Plural(friends, L"friend", L"friends");
    }
    }
    return L"";
}

static float DrawOverlayCaption(Graphics& g) {
    Font* f = F(12.0f, true);
    std::wstring caption = ViewCaption();
    float x = g_graphRect.X + S(18), y = g_graphRect.Y + S(18), h = S(32);
    float tw = MeasureText(g, caption, f) + S(42);
    RectF card(x, y, Min(tw, g_graphRect.Width - S(260)), h);
    FillRound(g, card, h / 2, Hex(theme::panel, 235));
    StrokeRound(g, card, h / 2, Hex(theme::border), S(1));
    UINT dot = g_viewMode == ViewMode::Focus ? theme::accentHi
             : g_viewMode == ViewMode::Search ? theme::path
             : g_viewMode == ViewMode::Overview ? theme::common : theme::muted;
    FillCircle(g, card.X + S(16), card.Y + h / 2, S(4), Hex(dot));
    Text(g, caption, f, RectF(card.X + S(28), card.Y, card.Width - S(36), h), Hex(theme::text));
    return y + h;
}

static void DrawLegend(Graphics& g, float y) {
    struct Item { UINT color; std::wstring label; bool line; };
    std::vector<Item> items;
    if (g_sel != -1) {
        items.push_back({theme::accentHi, L"Selected", false});
        if (g_target == -1) {
            items.push_back({theme::accent, L"Friends", true});
            if (!g_suggestions.empty() && g_viewMode == ViewMode::Focus) items.push_back({theme::faint, L"Suggested", false});
        } else {
            items.push_back({theme::target, L"Compared", false});
            if (!g_path.empty()) items.push_back({theme::path, L"Shortest path", true});
            if (!g_common.empty()) items.push_back({theme::common, L"Mutual friends", false});
        }
    } else if (!g_query.empty()) {
        items.push_back({theme::path, L"Matches your search", false});
    }
    if (items.empty()) return;
    Font* f = F(11.5f);
    float x = g_graphRect.X + S(18), h = S(30);
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
        Text(g, it.label, f, RectF(cx, y, tw + S(16), h), Hex(theme::muted));
        cx += tw + S(18);
    }
}

static void DrawBusyPill(Graphics& g) {
    Font* f = F(12.5f, true);
    float tw = MeasureText(g, g_busyLabel, f) + S(64);
    const RectF& r = g_graphRect;
    RectF pill(r.X + (r.Width - tw) / 2, r.Y + S(66), tw, S(38));
    FillRound(g, pill, S(19), Hex(theme::card));
    StrokeRound(g, pill, S(19), Hex(theme::border), S(1));
    float angle = (float)(GetTickCount64() % 1000) * 0.36f;
    float cx = pill.X + S(22), cy = pill.Y + pill.Height / 2;
    StrokeCircle(g, cx, cy, S(7), Hex(theme::border), S(2.2f));
    DrawArc(g, cx, cy, S(7), angle, 100, Hex(theme::accentHi), S(2.2f));
    Text(g, g_busyLabel, f, RectF(pill.X + S(38), pill.Y, pill.Width - S(46), pill.Height), Hex(theme::text));
}

static void PaintGraph(Graphics& g) {
    const RectF& r = g_graphRect;
    DrawGrid(g, r);
    g.SetClip(r);

    if (g_viewIds.empty()) {
        Text(g, g_net.userCount() == 0 ? L"No one here yet — add a person or generate a network." : L"",
             F(14.0f), r, Hex(theme::muted), StringAlignmentCenter);
    }

    bool focus = g_sel != -1;
    std::unordered_set<int> pathSet(g_path.begin(), g_path.end());
    std::unordered_set<int> commonSet(g_common.begin(), g_common.end());
    std::unordered_set<int> suggestSet;
    for (const auto& s : g_suggestions) suggestSet.insert(s.userId);
    auto isFriendOfSel = [&](int id) { return focus && g_net.areFriends(g_sel, id); };
    std::set<std::pair<int, int>> pathEdges;
    for (size_t i = 0; i + 1 < g_path.size(); ++i)
        pathEdges.insert({Min(g_path[i], g_path[i + 1]), Max(g_path[i], g_path[i + 1])});

    bool searching = !g_query.empty() && !focus;
    std::vector<char> matched(g_viewIds.size(), 0);
    if (searching) {
        for (size_t i = 0; i < g_viewIds.size(); ++i) {
            const User* u = g_net.getUser(g_viewIds[i]);
            matched[i] = u && Matches(*u, g_query);
        }
    }

    // Edges: regular first, highlighted on top.
    const size_t count = g_viewIds.size();
    bool dense = count > 300;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& l : g_layout.links) {
            int a = g_viewIds[l.first], b = g_viewIds[l.second];
            bool onPath = pathEdges.count({Min(a, b), Max(a, b)}) > 0;
            bool incident = focus && (a == g_sel || b == g_sel);
            bool mutualLink = g_target != -1 && ((a == g_target && commonSet.count(b)) || (b == g_target && commonSet.count(a)));
            bool highlighted = onPath || incident || mutualLink;
            if ((pass == 1) != highlighted) continue;

            PointF pa = ToScreen(g_layout.bodies[l.first]), pb = ToScreen(g_layout.bodies[l.second]);
            Color c;
            float w;
            if (onPath)          { c = Hex(theme::path);          w = S(3.2f); }
            else if (incident)   { c = Hex(theme::accentHi, g_target == -1 ? 150 : 90); w = S(1.8f); }
            else if (mutualLink) { c = Hex(theme::common, 150);   w = S(1.8f); }
            else if (searching && !(matched[l.first] && matched[l.second])) { c = Hex(theme::edge, 70); w = S(1.0f); }
            else                 { c = Hex(theme::edge, focus ? 100 : (dense ? 150 : 255)); w = S(dense ? 1.0f : 1.4f); }
            Pen pen(c, w);
            g.DrawLine(&pen, pa, pb);
        }
    }

    // Nodes: dimmed/regular first, highlighted on top.
    float zs = ZoomScale();
    Font* label = GetFont(std::round(S(11.5f) * zs * 2) / 2, false);
    Font* labelBold = GetFont(std::round(S(11.5f) * zs * 2) / 2, true);
    bool labelsForAll = count <= 60 ? g_zoom >= 0.4f : g_zoom >= 1.3f;
    bool initialsForAll = count <= 300 || g_zoom >= 0.9f;
    RectF cull(r.X - S(60), r.Y - S(60), r.Width + S(120), r.Height + S(120));

    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < count; ++i) {
            int id = g_viewIds[i];
            bool isSel = id == g_sel, isTgt = id == g_target;
            bool onPath = pathSet.count(id) > 0, isCommon = commonSet.count(id) > 0;
            bool isHover = id == g_hoverNode;
            bool isMatch = searching && matched[i];
            bool highlighted = isSel || isTgt || onPath || isCommon || isHover || isMatch;
            if ((pass == 1) != highlighted) continue;

            PointF p = ToScreen(g_layout.bodies[i]);
            if (!Contains(cull, p.X, p.Y)) continue;
            bool important = searching ? isMatch || isHover
                                       : (!focus || highlighted || isFriendOfSel(id) || suggestSet.count(id));
            bool suggested = focus && g_target == -1 && suggestSet.count(id) && !isFriendOfSel(id);
            BYTE a = important ? 255 : 60;
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
            } else if (isMatch) {
                StrokeCircle(g, p.X, p.Y, rad + S(4), Hex(theme::path), S(2.0f));
            }
            if (suggested) StrokeRound(g, RectF(p.X - rad - S(4), p.Y - rad - S(4), (rad + S(4)) * 2, (rad + S(4)) * 2),
                                       rad + S(4), Hex(theme::muted, 200), S(1.5f), true);
            if (isHover && !isSel && !isTgt) StrokeCircle(g, p.X, p.Y, rad + S(3), Color(110, 255, 255, 255), S(1.5f));

            DrawAvatar(g, id, p.X, p.Y, rad, a, initialsForAll || highlighted);

            bool emphasized = isSel || isTgt || onPath || isCommon || isHover;
            if (labelsForAll || emphasized || suggested || (isMatch && count <= 60)) {
                std::wstring name = NameOf(id);
                Font* lf = (isSel || isTgt) ? labelBold : label;
                float tw = MeasureText(g, name, lf) + S(14) * zs;
                float th = S(19) * zs;
                RectF pill(p.X - tw / 2, p.Y + rad + S(6), tw, th);
                FillRound(g, pill, th / 2, Hex(theme::bg, (BYTE)(a * 0.78f)));
                Text(g, name, lf, pill, Hex(important ? theme::text : theme::muted, a), StringAlignmentCenter);
            }
        }
    }

    float overlayBottom = DrawOverlayCaption(g);
    DrawLegend(g, overlayBottom + S(8));

    // Top-right controls: color mode, re-layout, fit
    float bh = S(32), right = r.X + r.Width - S(18);
    float bw = S(84);
    Button(g, RectF(right - bw, r.Y + S(18), bw, bh), L"Fit view", A_FIT, 0, Btn::Secondary, 12.0f);
    Button(g, RectF(right - bw * 2 - S(8), r.Y + S(18), bw, bh), L"Re-layout", A_RELAYOUT, 0, Btn::Secondary, 12.0f);
    {
        bool haveGroups = !g_insights.communityOf.empty();
        const wchar_t* modes[] = {L"People", L"Groups"};
        float segW = S(66), sx = right - bw * 2 - S(16) - segW * 2 - S(8);
        RectF seg(sx, r.Y + S(18), segW * 2 + S(8), bh);
        FillRound(g, seg, S(8), Hex(theme::card));
        StrokeRound(g, seg, S(8), Hex(theme::border), S(1));
        for (int i = 0; i < 2; ++i) {
            RectF b(seg.X + S(4) + i * segW, seg.Y + S(4), segW, bh - S(8));
            bool active = (int)g_colorMode == i;
            bool enabled = i == 0 || haveGroups;
            if (active) FillRound(g, b, S(6), Hex(theme::cardHover));
            else if (enabled && IsHot(A_COLOR_MODE, i)) FillRound(g, b, S(6), Hex(theme::cardHover, 120));
            Text(g, modes[i], F(11.5f, true), b, Hex(active ? theme::text : (enabled ? theme::muted : theme::faint)),
                 StringAlignmentCenter);
            if (enabled) AddHit(b, A_COLOR_MODE, i);
        }
    }

    Text(g, L"Click to select  ·  Right-click to compare  ·  Drag to move  ·  Scroll to zoom",
         F(11.5f), RectF(r.X + S(20), r.Y + r.Height - S(36), r.Width - S(40), S(20)), Hex(theme::faint));

    if (g_busy) DrawBusyPill(g);

    if (!g_toast.empty() && GetTickCount64() < g_toastUntil) {
        Font* f = F(12.5f, true);
        float tw = Min(MeasureText(g, g_toast, f) + S(64), r.Width - S(40));
        RectF t(r.X + (r.Width - tw) / 2, r.Y + r.Height - S(84), tw, S(38));
        FillRound(g, t, S(19), Hex(theme::card));
        StrokeRound(g, t, S(19), g_toastError ? Hex(theme::target, 140) : Hex(theme::border), S(1));
        FillCircle(g, t.X + S(20), t.Y + t.Height / 2, S(4), Hex(g_toastError ? theme::target : theme::common));
        Text(g, g_toast, f, RectF(t.X + S(32), t.Y, t.Width - S(44), t.Height), Hex(theme::text));
    }
    g.ResetClip();
}

// ---------------------------------------------------------------------------
// Painting: right panel (insights / profile)
// ---------------------------------------------------------------------------

static void PersonRow(Graphics& g, int id, float x, float y, float w, const std::wstring& trailing, int action = A_SELECT) {
    const User* u = g_net.getUser(id);
    if (!u) return;
    RectF row(x - S(8), y, w + S(16), S(42));
    bool hot = IsHot(action, id), tgt = id == g_target;
    if (tgt) FillRound(g, row, S(8), Hex(theme::target, 28));
    else if (hot) FillRound(g, row, S(8), Hex(theme::cardHover));
    DrawAvatar(g, id, row.X + S(24), row.Y + row.Height / 2, S(14));
    Text(g, Widen(u->name), F(12.5f, true), RectF(row.X + S(46), row.Y, row.Width - S(130), row.Height), Hex(theme::text));
    Text(g, trailing, F(11.5f), RectF(row.X + S(46), row.Y, row.Width - S(58), row.Height), Hex(theme::faint), StringAlignmentFar);
    AddHit(Intersect(row, g_rightRect), action, id);
}

static float PaintInsights(Graphics& g, float x, float y, float w) {
    float y0 = y;
    Text(g, L"Network insights", F(16.5f, true), RectF(x, y, w, S(24)), Hex(theme::text));
    y += S(26);
    Text(g, L"Select someone to see their profile.", F(12.0f), RectF(x, y, w, S(18)), Hex(theme::muted));
    y += S(30);

    bool current = InsightsCurrent();
    if (!current) {
        RectF note(x, y, w, S(44));
        FillRound(g, note, S(10), Hex(theme::path, 22));
        bool analyzing = g_busy && g_busyLabel.rfind(L"Analyzing", 0) == 0;
        Text(g, analyzing ? L"Crunching the numbers…" : L"Out of date since your last edit", F(12.0f, true),
             RectF(note.X + S(14), note.Y, w - S(110), note.Height), Hex(theme::path));
        if (!analyzing)
            Button(g, RectF(note.X + note.Width - S(84), note.Y + S(8), S(74), S(28)), L"Refresh", A_ANALYZE, 0,
                   Btn::Secondary, 11.5f);
        y += note.Height + S(14);
    }

    const Insights& in = g_insights;
    bool have = in.version >= 0;
    long long n = g_net.userCount(), e = g_net.connectionCount();
    wchar_t buf[64];
    auto dash = [&](bool ok, const std::wstring& s) { return ok ? s : std::wstring(L"—"); };
    swprintf(buf, 64, L"%.1f", n ? 2.0 * e / n : 0.0);
    std::wstring avg = buf;
    std::wstring coverage = L"—";
    if (have && in.stats.users > 0) {
        swprintf(buf, 64, L"%.1f%%", 100.0 * in.largestComponent / in.stats.users);
        coverage = buf;
    }
    struct Tile { std::wstring value, label; } tiles[] = {
        {FormatCount(n), L"People"},
        {FormatCount(e), L"Friendships"},
        {avg, L"Average friends"},
        {dash(have, FormatCount(in.stats.maxFriends)), L"Most friends"},
        {dash(have, FormatCount(in.communities)), L"Communities"},
        {dash(have, FormatCount(in.components)), L"Separate groups"},
        {coverage, L"In the largest group"},
        {dash(have, FormatCount(in.stats.isolatedUsers)), L"With no friends"},
    };
    float gap = S(8), tw = (w - gap) / 2, th = S(58);
    for (int i = 0; i < 8; ++i) {
        RectF t(x + (i % 2) * (tw + gap), y + (i / 2) * (th + gap), tw, th);
        FillRound(g, t, S(10), Hex(theme::card));
        Text(g, tiles[i].value, F(16.0f, true), RectF(t.X + S(12), t.Y + S(8), t.Width - S(16), S(24)), Hex(theme::text));
        Text(g, tiles[i].label, F(11.0f), RectF(t.X + S(12), t.Y + S(32), t.Width - S(16), S(16)), Hex(theme::muted));
    }
    y += 4 * (th + gap) + S(18);

    if (have && !in.mostConnected.empty()) {
        SectionLabel(g, x, y, w, L"MOST CONNECTED");
        y += S(22);
        for (int id : in.mostConnected) {
            if (!g_net.userExists(id)) continue;
            PersonRow(g, id, x, y, w, Plural(g_net.degree(id), L"friend", L"friends"));
            y += S(42);
        }
        y += S(18);
    }

    if (have && !in.largestCommunities.empty()) {
        SectionLabel(g, x, y, w, L"LARGEST COMMUNITIES", g_colorMode == NodeColoring::Communities ? L"" : L"colors in “Groups” view");
        y += S(24);
        int maxSize = in.largestCommunities.front().second;
        for (size_t i = 0; i < Min<size_t>(5, in.largestCommunities.size()); ++i) {
            int c = in.largestCommunities[i].first, size = in.largestCommunities[i].second;
            FillCircle(g, x + S(6), y + S(10), S(5), Hex(PaletteColor(c)));
            Text(g, L"Community " + std::to_wstring(c + 1), F(12.0f, true), RectF(x + S(20), y, S(110), S(20)), Hex(theme::text));
            float barX = x + S(124), barW = w - S(124) - S(64);
            FillRound(g, RectF(barX, y + S(7), barW, S(6)), S(3), Hex(theme::card));
            FillRound(g, RectF(barX, y + S(7), Max(S(6), barW * size / maxSize), S(6)), S(3), Hex(PaletteColor(c)));
            Text(g, FormatCount(size), F(11.5f), RectF(x, y, w, S(20)), Hex(theme::muted), StringAlignmentFar);
            y += S(28);
        }
        y += S(16);
    }

    SectionLabel(g, x, y, w, L"SHORTCUTS");
    y += S(24);
    const wchar_t* keys[][2] = {
        {L"Right-click", L"Compare with selection"}, {L"Ctrl+F", L"Search people"},
        {L"Ctrl+O / S", L"Open / save"},           {L"Ctrl+G", L"Generate a network"},
        {L"Ctrl+N", L"Add a person"},             {L"Delete", L"Delete selected person"},
        {L"Esc", L"Clear selection"},             {L"F / R", L"Fit view / re-layout"},
    };
    Font* kf = F(11.0f, true);
    for (auto& k : keys) {
        float kw = MeasureText(g, k[0], kf) + S(16);
        RectF kr(x, y, kw, S(24));
        FillRound(g, kr, S(6), Hex(theme::card));
        StrokeRound(g, kr, S(6), Hex(theme::border), S(1));
        Text(g, k[0], kf, kr, Hex(theme::text), StringAlignmentCenter);
        Text(g, k[1], F(12.0f), RectF(x + S(104), y, w - S(104), S(24)), Hex(theme::muted));
        y += S(31);
    }
    return y - y0;
}

static float PaintProfile(Graphics& g, float x, float y, float w) {
    float y0 = y;
    const User* u = g_net.getUser(g_sel);
    if (!u) return 0;

    // Header
    DrawAvatar(g, u->id, x + S(30), y + S(30), S(30));
    float tx = x + S(76), tw = w - S(76);
    Text(g, Widen(u->name), F(17.0f, true), RectF(tx, y + S(2), tw, S(26)), Hex(theme::text));
    Text(g, L"@" + Widen(u->username), F(12.5f), RectF(tx, y + S(27), tw, S(18)), Hex(theme::muted));
    int deg = g_net.degree(u->id);
    std::wstring meta = L"Age " + std::to_wstring(u->age) + L"  ·  " + Plural(deg, L"friend", L"friends");
    int community = CommunityOf(u->id);
    if (community >= 0 && InsightsCurrent()) meta += L"  ·  Community " + std::to_wstring(community + 1);
    Text(g, meta, F(12.0f), RectF(tx, y + S(45), tw, S(18)), Hex(theme::faint));
    y += S(84);

    // Interests
    SectionLabel(g, x, y, w, L"INTERESTS");
    y += S(24);
    std::vector<Chip> chips;
    for (const auto& i : u->interests)
        if (!i.empty() && i != "None Provided") chips.push_back({Widen(i), Hex(theme::card), Hex(theme::text), Color(), false});
    if (chips.empty()) {
        Text(g, L"No interests listed", F(12.5f), RectF(x, y, w, S(20)), Hex(theme::muted));
        y += S(20);
    } else {
        y += Chips(g, x, y, w, chips);
    }
    y += S(26);

    // Compare
    SectionLabel(g, x, y, w, L"COMPARE");
    y += S(24);
    if (g_target == -1) {
        RectF hint(x, y, w, S(66));
        StrokeRound(g, hint, S(10), Hex(theme::border), S(1.2f), true);
        WrappedText(g, L"Right-click another person (or Ctrl+click) to find the shortest path and your mutual friends.",
                    F(12.0f), hint.X + S(16), hint.Y + S(13), hint.Width - S(32), Hex(theme::muted));
        y += hint.Height + S(26);
    } else {
        const User* t = g_net.getUser(g_target);
        RectF card(x, y, w, S(56));
        FillRound(g, card, S(10), Hex(theme::card));
        FillRound(g, RectF(card.X, card.Y + S(12), S(3), card.Height - S(24)), S(1.5f), Hex(theme::target));
        DrawAvatar(g, g_target, card.X + S(30), card.Y + card.Height / 2, S(16));
        Text(g, Widen(t->name), F(13.0f, true), RectF(card.X + S(56), card.Y + S(9), card.Width - S(100), S(20)), Hex(theme::text));
        Text(g, L"@" + Widen(t->username), F(11.5f), RectF(card.X + S(56), card.Y + S(28), card.Width - S(100), S(18)), Hex(theme::muted));
        Button(g, RectF(card.X + card.Width - S(42), card.Y + S(12), S(32), S(32)), L"✕", A_CLEAR_TARGET, 0, Btn::Ghost, 12.0f);
        y += card.Height + S(18);

        if (g_path.empty()) {
            Text(g, L"∞", F(26.0f, true), RectF(x, y, S(40), S(36)), Hex(theme::faint));
            Text(g, L"Not connected", F(13.0f, true), RectF(x + S(42), y + S(1), w - S(42), S(18)), Hex(theme::text));
            Text(g, L"No path exists between them yet", F(11.5f), RectF(x + S(42), y + S(19), w - S(42), S(18)), Hex(theme::muted));
            y += S(48);
        } else {
            int hops = (int)g_path.size() - 1;
            std::wstring desc = hops == 1 ? L"Direct friends" : (hops == 2 ? L"Friends of friends" : std::to_wstring(hops) + L" hops apart");
            Text(g, std::to_wstring(hops), F(26.0f, true), RectF(x, y - S(2), S(40), S(40)), Hex(theme::path));
            Text(g, desc, F(13.0f, true), RectF(x + S(42), y + S(1), w - S(42), S(18)), Hex(theme::text));
            Text(g, hops == 1 ? L"degree of separation" : L"degrees of separation", F(11.5f),
                 RectF(x + S(42), y + S(19), w - S(42), S(18)), Hex(theme::muted));
            y += S(50);
            std::vector<Chip> pc;
            for (int id : g_path) {
                UINT c = id == g_sel ? theme::accentHi : (id == g_target ? theme::target : theme::path);
                pc.push_back({FirstName(id), Hex(c, 34), Hex(theme::text), Hex(c), true});
            }
            y += Chips(g, x, y, w, pc, L"›");
            y += S(22);
        }

        SectionLabel(g, x, y, w, L"MUTUAL FRIENDS", FormatCount((long long)g_common.size()));
        y += S(24);
        if (g_common.empty()) {
            Text(g, L"None in common", F(12.5f), RectF(x, y, w, S(20)), Hex(theme::muted));
            y += S(20);
        } else {
            std::vector<Chip> cc;
            for (size_t i = 0; i < Min<size_t>(g_common.size(), 12); ++i)
                cc.push_back({NameOf(g_common[i]), Hex(theme::common, 30), Hex(theme::text), Hex(theme::common), true});
            if (g_common.size() > 12) cc.push_back({L"+" + FormatCount((long long)g_common.size() - 12) + L" more", Hex(theme::card), Hex(theme::muted), Color(), false});
            y += Chips(g, x, y, w, cc);
        }
        y += S(20);

        if (g_net.areFriends(g_sel, g_target))
            Button(g, RectF(x, y, w, S(40)), L"Remove connection", A_DISCONNECT, 0, Btn::Danger);
        else
            Button(g, RectF(x, y, w, S(40)), L"Connect " + FirstName(g_sel) + L" & " + FirstName(g_target), A_CONNECT, 0, Btn::Primary);
        y += S(40) + S(28);
    }

    // People you may know
    if (!g_suggestions.empty()) {
        SectionLabel(g, x, y, w, L"PEOPLE YOU MAY KNOW");
        y += S(22);
        for (const auto& s : g_suggestions) {
            const User* su = g_net.getUser(s.userId);
            if (!su) continue;
            RectF row(x - S(8), y, w + S(16), S(48));
            if (IsHot(A_SELECT, s.userId)) FillRound(g, row, S(8), Hex(theme::cardHover));
            DrawAvatar(g, s.userId, row.X + S(24), row.Y + row.Height / 2, S(15));
            Text(g, Widen(su->name), F(12.5f, true), RectF(row.X + S(48), row.Y + S(6), row.Width - S(130), S(18)), Hex(theme::text));
            std::wstring why = s.mutualFriends > 0 ? Plural(s.mutualFriends, L"mutual friend", L"mutual friends") : L"";
            if (s.sharedInterests > 0) {
                if (!why.empty()) why += L" · ";
                why += Plural(s.sharedInterests, L"shared interest", L"shared interests");
            }
            Text(g, why, F(11.0f), RectF(row.X + S(48), row.Y + S(24), row.Width - S(130), S(16)), Hex(theme::muted));
            AddHit(Intersect(row, g_rightRect), A_SELECT, s.userId);
            Button(g, RectF(row.X + row.Width - S(72), row.Y + S(10), S(64), S(28)), L"+ Add", A_ADD_SUGGESTION, s.userId,
                   Btn::Secondary, 11.5f);
            y += S(50);
        }
        y += S(20);
    }

    // Friends (most connected first)
    std::vector<int> friends = g_net.getFriends(u->id);
    KeepMostConnected(friends, kFriendsListed);
    SectionLabel(g, x, y, w, L"FRIENDS", FormatCount(deg));
    y += S(22);
    if (friends.empty()) {
        y += WrappedText(g, L"No friends yet. Add someone from “People you may know”, or right-click a person and Connect.",
                         F(12.5f), x, y, w, Hex(theme::muted));
    }
    for (int f : friends) {
        PersonRow(g, f, x, y, w, Plural(g_net.degree(f), L"friend", L"friends"));
        y += S(42);
    }
    if (deg > kFriendsListed) {
        Text(g, L"+ " + FormatCount(deg - kFriendsListed) + L" more", F(12.0f), RectF(x, y + S(4), w, S(20)), Hex(theme::muted));
        y += S(28);
    }
    y += S(24);

    Button(g, RectF(x, y, w, S(38)),
           g_confirmDelete == g_sel ? L"Click again to delete " + FirstName(g_sel) : std::wstring(L"Delete person"),
           A_DELETE_USER, 0, Btn::Danger, 12.5f);
    y += S(38);
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
    float used = g_sel == -1 ? PaintInsights(g, x, top, w) : PaintProfile(g, x, top, w);
    g.ResetClip();

    g_rightContentH = used + S(52);
    float maxScroll = Max(0.0f, g_rightContentH - r.Height);
    if (g_rightScroll > maxScroll) { g_rightScroll = maxScroll; Invalidate(); }
    DrawScrollbar(g, r, g_rightContentH, g_rightScroll);
}

// ---------------------------------------------------------------------------
// Painting: "Generate a network" sheet
// ---------------------------------------------------------------------------

static void PaintGenerateSheet(Graphics& g, const RectF& client) {
    hits.clear();  // the sheet is modal: only its own regions are clickable
    AddHit(client, A_MODAL_CLOSE, 0);
    SolidBrush dim(Color(170, 6, 7, 11));
    g.FillRectangle(&dim, client);

    float w = S(480), rowH = S(58);
    float h = S(112) + rowH * kPresetCount + S(70);
    RectF card(client.X + (client.Width - w) / 2, client.Y + (client.Height - h) / 2, w, h);
    FillRound(g, card, S(14), Hex(theme::panel));
    StrokeRound(g, card, S(14), Hex(theme::border), S(1));
    AddHit(card, A_NONE, 0);

    float x = card.X + S(28), cw = w - S(56), y = card.Y + S(26);
    Text(g, L"Generate a network", F(19.0f, true), RectF(x, y, cw, S(28)), Hex(theme::text));
    y += S(30);
    WrappedText(g, L"Synthetic people who cluster into communities, share interests, and include a few very popular hubs.",
                F(12.0f), x, y, cw, Hex(theme::muted));
    y = card.Y + S(112);

    for (int i = 0; i < kPresetCount; ++i) {
        RectF row(x - S(10), y, cw + S(20), rowH - S(6));
        bool hot = IsHot(A_GENERATE, i);
        FillRound(g, row, S(10), Hex(hot ? theme::cardHover : theme::card));
        UINT color = PaletteColor(i);
        RectF icon(row.X + S(12), row.Y + (row.Height - S(30)) / 2, S(30), S(30));
        FillRound(g, icon, S(8), Hex(color, 45));
        int dots = Min(i + 1, 5);
        for (int d = 0; d < dots; ++d) {
            float a = d * 6.2832f / dots;
            float rr = dots == 1 ? 0 : S(6);
            FillCircle(g, icon.X + S(15) + rr * std::cos(a), icon.Y + S(15) + rr * std::sin(a), S(2.6f), Hex(color));
        }
        Text(g, kPresets[i].title, F(13.0f, true), RectF(row.X + S(54), row.Y + S(7), row.Width - S(200), S(20)), Hex(theme::text));
        Text(g, kPresets[i].detail, F(11.5f), RectF(row.X + S(54), row.Y + S(26), row.Width - S(64), S(18)), Hex(theme::muted));
        Text(g, kPresets[i].hint, F(11.0f), RectF(row.X, row.Y + S(7), row.Width - S(14), S(20)), Hex(theme::faint), StringAlignmentFar);
        AddHit(row, A_GENERATE, i);
        y += rowH;
    }

    Button(g, RectF(card.X + card.Width - S(28) - S(96), card.Y + card.Height - S(56), S(96), S(36)), L"Cancel",
           A_MODAL_CLOSE, 0, Btn::Secondary, 12.5f);
    Text(g, g_dirty ? L"You'll be asked to save your changes first." : L"Replaces the network that's open now.",
         F(11.5f), RectF(x, card.Y + card.Height - S(56), cw - S(110), S(36)), Hex(theme::faint));
}

static void PaintAll(Graphics& g, const RectF& client) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Hex(theme::bg));
    hits.clear();
    PaintGraph(g);
    PaintSidebar(g);
    PaintRight(g);
    if (g_modalGenerate) PaintGenerateSheet(g, client);
}

static void Layout(int w, int h) {
    float sw = S(296), rw = S(356);
    g_sidebarRect = RectF(0, 0, sw, (float)h);
    g_rightRect = RectF(w - rw, 0, rw, (float)h);
    g_graphRect = RectF(sw, 0, Max(0.0f, w - sw - rw), (float)h);
    if (g_hSearch) {
        float x = S(20) + S(34), y = S(238), bw = sw - S(40) - S(34) - S(40);
        TEXTMETRICW tm;
        HDC dc = GetDC(g_hSearch);
        HGDIOBJ old = SelectObject(dc, g_searchFont);
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(g_hSearch, dc);
        int eh = tm.tmHeight + (int)S(2);
        MoveWindow(g_hSearch, (int)x, (int)(y + (S(38) - eh) / 2), (int)bw, eh, TRUE);
    }
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
    else if (username.find_first_of(" \t") != std::string::npos) { g_dlgError = L"Usernames can't contain spaces."; bad = 0; }
    else if (g_net.findUserByUsername(username) != -1) { g_dlgError = L"That username is already taken."; bad = 0; }
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
    if (id == -1) {
        g_dlgError = L"Couldn't add this person.";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    CloseDialog(hwnd);
    OnNetworkEdited();
    Select(id);
    Toast(L"Welcome, " + FirstName(id) + L"! Add friends from “People you may know”.");
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
    Text(g, L"Add a person", F(19.0f, true), RectF(pad, S(22), w, S(28)), Hex(theme::text));
    Text(g, L"They'll join the network right away.", F(12.5f), RectF(pad, S(50), w, S(20)), Hex(theme::muted));

    HWND focus = GetFocus();
    for (int i = 0; i < kFieldCount; ++i) {
        const DialogField& f = g_fields[i];
        RectF labelR(f.box.X, f.box.Y - S(22), f.box.Width, S(18));
        Text(g, f.label, F(12.0f, true), labelR, Hex(theme::text));
        if (*f.hint) Text(g, f.hint, F(11.5f), labelR, Hex(theme::faint), StringAlignmentFar);
        FillRound(g, f.box, S(8), Hex(theme::card));
        bool focused = focus == f.edit;
        StrokeRound(g, f.box, S(8), focused ? Hex(theme::accent) : Hex(theme::border), focused ? S(1.6f) : S(1));
    }
    if (!g_dlgError.empty()) {
        float ey = g_fields[kFieldCount - 1].box.Y + g_fields[kFieldCount - 1].box.Height + S(14);
        FillCircle(g, pad + S(4), ey + S(9), S(3.5f), Hex(theme::target));
        Text(g, g_dlgError, F(12.0f), RectF(pad + S(14), ey, w - S(14), S(18)), Hex(theme::target));
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
        FillRound(g, inner, S(8), Hex(theme::accent, pressed ? 200 : 255));
        if (focused) StrokeRound(g, inner, S(8), Hex(theme::accentHi), S(1.5f));
    } else {
        FillRound(g, inner, S(8), Hex(pressed ? theme::cardHover : theme::card));
        StrokeRound(g, inner, S(8), focused ? Hex(theme::muted) : Hex(theme::border), S(1));
    }
    wchar_t text[64];
    GetWindowTextW(d->hwndItem, text, 64);
    Text(g, text, F(13.0f, true), r, primary ? Color(255, 255, 255, 255) : Hex(theme::text), StringAlignmentCenter);
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
        CreateWindowExW(0, L"BUTTON", L"Add person", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        (int)(rc.right - pad - S(132)), (int)by, (int)S(132), (int)bh, hwnd, (HMENU)IDOK, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        (int)(rc.right - pad - S(132) - S(10) - S(96)), (int)by, (int)S(96), (int)bh, hwnd,
                        (HMENU)IDCANCEL, inst, nullptr);
        g_dlgError.clear();
        SetFocus(g_fields[0].edit);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, Ref(theme::text));
        SetBkColor(dc, Ref(theme::card));
        return (LRESULT)g_cardBrush;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == IDOK) { SubmitDialog(hwnd); return 0; }
        if (id == IDCANCEL) { CloseDialog(hwnd); return 0; }
        if (id >= IDC_FIELD_BASE && id < IDC_FIELD_BASE + kFieldCount && (code == EN_SETFOCUS || code == EN_KILLFOCUS))
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
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, (UINT)(dpi * 96));
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
    int node = (!h && !g_modalGenerate && Contains(g_graphRect, x, y)) ? NodeAt(x, y) : -1;
    if (ha != hotAction || hp != hotParam || node != g_hoverNode) {
        hotAction = ha;
        hotParam = hp;
        g_hoverNode = node;
        Invalidate();
    }
    g_wantHand = (h != nullptr && h->action != A_NONE && h->action != A_MODAL_CLOSE) || node != -1;
}

static void CreateSearchFont() {
    if (g_searchFont) DeleteObject(g_searchFont);
    g_searchFont = CreateFontW(-(int)S(13.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (g_hSearch) SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_searchFont, TRUE);
}

static void OnKeyDown(WPARAM key) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (g_modalGenerate) {
        if (key == VK_ESCAPE) ShowGenerateSheet(false);
        else if (key >= '1' && key < (WPARAM)('1' + kPresetCount)) Generate((int)(key - '1'));
        Invalidate();
        return;
    }
    if (ctrl) {
        switch (key) {
        case 'N': DoAction(A_ADD_USER, 0); break;
        case 'O': DoAction(A_OPEN, 0); break;
        case 'S': if (shift) SaveAsync(true); else DoAction(A_SAVE, 0); break;
        case 'G': DoAction(A_GENERATE_MENU, 0); break;
        case 'F': SetFocus(g_hSearch); SendMessageW(g_hSearch, EM_SETSEL, 0, -1); Invalidate(); break;
        }
        return;
    }
    switch (key) {
    case VK_ESCAPE:
        if (g_target != -1) SetTarget(-1);
        else if (g_sel != -1) Select(-1);
        else if (GetWindowTextLengthW(g_hSearch) > 0) DoAction(A_CLEAR_SEARCH, 0);
        break;
    case VK_DELETE: DeleteSelected(); break;
    case 'F': DoAction(A_FIT, 0); break;
    case 'R': DoAction(A_RELAYOUT, 0); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        ApplyDarkTitleBar(hwnd);
        g_hSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
                                    (HMENU)(INT_PTR)IDC_SEARCH, GetModuleHandleW(nullptr), nullptr);
        CreateSearchFont();
        SendMessageW(g_hSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SendMessageW(g_hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search people or interests");
        DragAcceptFiles(hwnd, TRUE);
        StartSim();
        return 0;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        wchar_t path[4096];
        bool got = DragQueryFileW(drop, 0, path, 4096) > 0;
        DragFinish(drop);
        if (got && CheckIdle() && ConfirmDiscard()) LoadAsync(path);
        return 0;
    }

    case WM_SIZE:
        Layout(LOWORD(lParam), HIWORD(lParam));
        if (g_autoFit) StartSim();
        Invalidate();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = (LONG)S(1120);
        mm->ptMinTrackSize.y = (LONG)S(680);
        return 0;
    }

    case WM_DPICHANGED: {
        SetDpi(HIWORD(wParam) / 96.0f);
        CreateSearchFont();
        RECT* r = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_APP_JOB_DONE:
        OnJobDone((JobResult*)lParam);
        return 0;

    case WM_TIMER:
        switch (wParam) {
        case TIMER_SIM: {
            bool hot = g_layout.active() || g_dragNode != -1;
            if (hot) g_layout.tick();
            bool settled = g_autoFit ? FitView(false) : true;
            if (!hot && settled) KillTimer(hwnd, TIMER_SIM);
            Invalidate();
            break;
        }
        case TIMER_TOAST:
            KillTimer(hwnd, TIMER_TOAST);
            g_toast.clear();
            Invalidate();
            break;
        case TIMER_SEARCH:
            ApplySearch();
            break;
        case TIMER_SPIN:
            Invalidate();
            break;
        case TIMER_CONFIRM:
            KillTimer(hwnd, TIMER_CONFIRM);
            g_confirmDelete = -1;
            Invalidate();
            break;
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SEARCH) {
            int code = HIWORD(wParam);
            if (code == EN_CHANGE) SetTimer(hwnd, TIMER_SEARCH, g_net.userCount() > 200000 ? 250 : 80, nullptr);
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS || code == EN_CHANGE) Invalidate();
        }
        return 0;

    case WM_CTLCOLOREDIT:
        if ((HWND)lParam == g_hSearch) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, Ref(theme::text));
            SetBkColor(dc, Ref(theme::card));
            return (LRESULT)g_cardBrush;
        }
        break;

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
            PaintAll(g, RectF(0, 0, (REAL)rc.right, (REAL)rc.bottom));
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && (HWND)wParam == hwnd) {
            LPCWSTR c = IDC_ARROW;
            if (g_panning && g_mouseMoved) c = IDC_SIZEALL;
            else if (g_dragNode != -1 || g_wantHand) c = IDC_HAND;
            else if (g_busy) c = IDC_APPSTARTING;
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
        auto drag = g_dragNode != -1 ? g_viewIndex.find(g_dragNode) : g_viewIndex.end();
        if (drag != g_viewIndex.end()) {
            ForceLayout::Body& b = g_layout.bodies[drag->second];
            ToWorld(x, y, b.x, b.y);
            b.fixed = true;
            b.vx = b.vy = 0;
            if (g_mouseMoved) g_autoFit = false;
            g_layout.alphaTarget = 0.25f;
            g_layout.reheat(0.25f);
            StartSim();
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
            hotAction = A_NONE;
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
            pressAction = h->action;
            pressParam = h->param;
            if ((wParam & MK_CONTROL) && h->action == A_SELECT) {
                SetTarget(h->param);
                pressAction = A_NONE;
            }
            Invalidate();
            return 0;
        }
        if (!g_modalGenerate && Contains(g_graphRect, x, y)) {
            int node = NodeAt(x, y);
            if (node != -1) {
                if (wParam & MK_CONTROL) SetTarget(node);
                else if (node != g_sel) Select(node);
                g_dragNode = g_viewIndex.count(node) ? node : -1;
            } else {
                g_panning = true;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        int pa = pressAction, pp = pressParam;
        bool wasPanning = g_panning;
        pressAction = A_NONE;
        if (g_dragNode != -1) {
            auto it = g_viewIndex.find(g_dragNode);
            if (it != g_viewIndex.end()) g_layout.bodies[it->second].fixed = false;
            g_dragNode = -1;
            g_layout.alphaTarget = 0.0f;
        }
        g_panning = false;
        ReleaseCapture();
        if (pa != A_NONE) {
            const Hit* h = HitAt(x, y);
            if (h && h->action == pa && h->param == pp) DoAction(pa, pp);
        } else if (wasPanning && !g_mouseMoved) {
            if (g_target != -1) SetTarget(-1);
            else Select(-1);
        }
        UpdateHover(x, y);
        Invalidate();
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (g_modalGenerate) return 0;
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
        if (g_dragNode != -1) {
            auto it = g_viewIndex.find(g_dragNode);
            if (it != g_viewIndex.end()) g_layout.bodies[it->second].fixed = false;
            g_layout.alphaTarget = 0.0f;
        }
        g_dragNode = -1;
        g_panning = false;
        return 0;

    case WM_MOUSEWHEEL: {
        if (g_modalGenerate) return 0;
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        float x = (float)pt.x, y = (float)pt.y;
        float d = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
        if (Contains(g_listRect, x, y)) {
            float contentH = S(54) * g_listIds.size();
            g_listScroll = Clamp(g_listScroll - d * S(80), 0.0f, Max(0.0f, contentH - g_listRect.Height));
        } else if (Contains(g_rightRect, x, y)) {
            g_rightScroll = Clamp(g_rightScroll - d * S(80), 0.0f, Max(0.0f, g_rightContentH - g_rightRect.Height));
        } else if (Contains(g_graphRect, x, y)) {
            float wx, wy;
            ToWorld(x, y, wx, wy);
            g_zoom = Clamp(g_zoom * std::pow(1.15f, d), 0.1f, 4.0f);
            g_camX = wx - (x - g_graphRect.X - g_graphRect.Width / 2) / Scale();
            g_camY = wy - (y - g_graphRect.Y - g_graphRect.Height / 2) / Scale();
            g_autoFit = false;
        }
        Invalidate();
        UpdateHover(x, y);
        return 0;
    }

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_CLOSE:
        if (g_busy && g_busyLabel.rfind(L"Saving", 0) == 0) {
            Toast(L"Still saving — one moment");
            return 0;
        }
        if (!ConfirmDiscard()) return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Keys typed into the search box: Enter picks the first result, Esc clears,
// and Ctrl shortcuts still reach the main window.
static bool RouteSearchKeys(const MSG& msg) {
    if (msg.hwnd != g_hSearch || msg.message != WM_KEYDOWN) return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    switch (msg.wParam) {
    case VK_RETURN:
        ApplySearch();
        if (!g_listIds.empty()) Select(g_listIds.front());
        SetFocus(g_hwnd);
        return true;
    case VK_ESCAPE:
        if (GetWindowTextLengthW(g_hSearch) > 0) SetWindowTextW(g_hSearch, L"");
        SetFocus(g_hwnd);
        Invalidate();
        return true;
    case 'N': case 'O': case 'S': case 'G':
        if (ctrl) { OnKeyDown(msg.wParam); return true; }
        break;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartup(&gdiplusToken, &gsi, nullptr);

    g_cardBrush = CreateSolidBrush(Ref(theme::card));
    SetDpi(GetDpiForSystem() / 96.0f);

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
    int w = Min((int)S(1440), (int)(work.right - work.left) - 40);
    int h = Min((int)S(880), (int)(work.bottom - work.top) - 40);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Social Graph", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                                w, h, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Window creation failed.", L"Social Graph", MB_ICONERROR);
        return 1;
    }

    // The window may land on a monitor with a different DPI than the system default.
    float scale = GetDpiForWindow(hwnd) / 96.0f;
    if (scale != dpi) {
        SetDpi(scale);
        CreateSearchFont();
    }
    RECT rc;
    GetClientRect(hwnd, &rc);
    Layout(rc.right, rc.bottom);

    SeedSample(g_net);
    OnNetworkReplaced();
    FitView(true);

    // "SocialGraph.exe network.sgraph" (e.g. Open with...) opens that file.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) LoadAsync(argv[1]);
    if (argv) LocalFree(argv);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (RouteSearchKeys(msg)) continue;
        if (g_hDlg && IsDialogMessageW(g_hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // A background job may still be reading the network; don't run destructors under it.
    if (g_busy) ExitProcess((UINT)msg.wParam);

    ReleaseResources();
    if (g_searchFont) DeleteObject(g_searchFont);
    if (g_editFont) DeleteObject(g_editFont);
    if (g_cardBrush) DeleteObject(g_cardBrush);
    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
