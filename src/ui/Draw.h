// Drawing toolkit for the Social Graph desktop app: theme colors, DPI scaling,
// cached fonts, GDI+ primitives, and immediate-mode hit regions for the
// custom-painted buttons, rows and chips.
#ifndef UI_DRAW_H
#define UI_DRAW_H

#include <windows.h>
#include <gdiplus.h>

#include <string>
#include <vector>

namespace ui {

namespace theme {
constexpr UINT bg        = 0x0E1016;
constexpr UINT panel     = 0x141720;
constexpr UINT card      = 0x1B1F2A;
constexpr UINT cardHover = 0x242938;
constexpr UINT border    = 0x272C3A;
constexpr UINT text      = 0xE7E9F0;
constexpr UINT muted     = 0x8A91A6;
constexpr UINT faint     = 0x5D6479;
constexpr UINT accent    = 0x6E7BFF;
constexpr UINT accentHi  = 0x8591FF;
constexpr UINT target    = 0xFF6B9A;
constexpr UINT path      = 0xFFB547;
constexpr UINT common    = 0x34D399;
constexpr UINT edge      = 0x343B4E;
constexpr UINT grid      = 0x252A37;
constexpr UINT unknown   = 0x4A5166;
}  // namespace theme

template <class T> T Min(T a, T b) { return a < b ? a : b; }
template <class T> T Max(T a, T b) { return a > b ? a : b; }
template <class T> T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// DPI scale factor (1.0 at 96 DPI). S() converts design pixels to device pixels.
extern float dpi;
inline float S(float v) { return v * dpi; }
void SetDpi(float scale);  // also drops cached fonts

Gdiplus::Color Hex(UINT rgb, BYTE alpha = 255);
COLORREF Ref(UINT rgb);
UINT PaletteColor(int key);  // one of ten distinct accent colors

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& w);
std::wstring FormatCount(long long n);    // 1,234,567
std::wstring CompactCount(long long n);   // 1.2M

// Fonts (Segoe UI), cached per size. F() takes a size in design points.
Gdiplus::Font* GetFont(float px, bool semibold);
inline Gdiplus::Font* F(float pt, bool semibold = false) { return GetFont(S(pt), semibold); }
void ReleaseResources();  // call before GdiplusShutdown

bool Contains(const Gdiplus::RectF& r, float x, float y);
Gdiplus::RectF Intersect(const Gdiplus::RectF& a, const Gdiplus::RectF& b);

void AddRound(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float radius);
void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Color& c);
void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Color& c, float width,
                 bool dashed = false);
void FillCircle(Gdiplus::Graphics& g, float cx, float cy, float r, const Gdiplus::Color& c);
void StrokeCircle(Gdiplus::Graphics& g, float cx, float cy, float r, const Gdiplus::Color& c, float width);
void DrawArc(Gdiplus::Graphics& g, float cx, float cy, float r, float start, float sweep, const Gdiplus::Color& c,
             float width);

void Text(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* f, const Gdiplus::RectF& r,
          const Gdiplus::Color& c, Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear,
          Gdiplus::StringAlignment v = Gdiplus::StringAlignmentCenter);
// Word-wrapped text; returns the height used.
float WrappedText(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* f, float x, float y, float w,
                  const Gdiplus::Color& c, Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear);
float MeasureText(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* f);

// Immediate-mode hit regions, rebuilt on every paint.
struct Hit {
    Gdiplus::RectF r;
    int action;
    int param;
};
extern std::vector<Hit> hits;
extern int hotAction, hotParam, pressAction, pressParam;
void AddHit(const Gdiplus::RectF& r, int action, int param);
const Hit* HitAt(float x, float y);
inline bool IsHot(int a, int p) { return hotAction == a && hotParam == p; }
inline bool IsPressed(int a, int p) { return pressAction == a && pressParam == p && IsHot(a, p); }

enum class Btn { Primary, Secondary, Danger, Ghost };
void Button(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const std::wstring& label, int action, int param, Btn style,
            float fontPt = 13.0f, bool enabled = true);
void SectionLabel(Gdiplus::Graphics& g, float x, float y, float w, const std::wstring& s,
                  const std::wstring& right = L"");

struct Chip {
    std::wstring text;
    Gdiplus::Color bg, fg;
    Gdiplus::Color dot;
    bool hasDot = false;
};
// Lays chips out left-to-right with wrapping; returns the height used.
float Chips(Gdiplus::Graphics& g, float x, float y, float w, const std::vector<Chip>& chips,
            const std::wstring& separator = L"");

}  // namespace ui

#endif
