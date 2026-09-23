#include "Draw.h"

#include <map>
#include <memory>

using namespace Gdiplus;

namespace ui {

float dpi = 1.0f;
std::vector<Hit> hits;
int hotAction = 0, hotParam = 0, pressAction = 0, pressParam = 0;

static std::map<int, std::unique_ptr<Font>> g_fonts;

static const UINT kPalette[] = {
    0x6E7BFF, 0x34D399, 0xF59E0B, 0xFF6B9A, 0x38BDF8,
    0xA78BFA, 0xF472B6, 0x2DD4BF, 0xFB923C, 0x84CC16,
};

void SetDpi(float scale) {
    dpi = scale;
    g_fonts.clear();
}

void ReleaseResources() { g_fonts.clear(); }

Color Hex(UINT rgb, BYTE a) { return Color(a, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
COLORREF Ref(UINT rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

UINT PaletteColor(int key) {
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[((key % n) + n) % n];
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FormatCount(long long n) {
    std::wstring digits = std::to_wstring(n < 0 ? -n : n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += L',';
        out += digits[i];
    }
    return n < 0 ? L"-" + out : out;
}

std::wstring CompactCount(long long n) {
    wchar_t buf[32];
    if (n >= 9950000) swprintf(buf, 32, L"%.0fM", n / 1e6);
    else if (n >= 999500) swprintf(buf, 32, L"%.1fM", n / 1e6);  // 999,999 reads "1.0M", not "1000K"
    else if (n >= 100000) swprintf(buf, 32, L"%.0fK", n / 1e3);
    else return FormatCount(n);
    return buf;
}

Font* GetFont(float px, bool semibold) {
    int key = (int)(px * 4.0f) * 2 + (semibold ? 1 : 0);
    auto& f = g_fonts[key];
    if (!f) {
        f.reset(new Font(semibold ? L"Segoe UI Semibold" : L"Segoe UI", px, FontStyleRegular, UnitPixel));
        if (f->GetLastStatus() != Ok)
            f.reset(new Font(L"Segoe UI", px, semibold ? FontStyleBold : FontStyleRegular, UnitPixel));
    }
    return f.get();
}

bool Contains(const RectF& r, float x, float y) {
    return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height;
}

RectF Intersect(const RectF& a, const RectF& b) {
    float x1 = Max(a.X, b.X), y1 = Max(a.Y, b.Y);
    float x2 = Min(a.X + a.Width, b.X + b.Width), y2 = Min(a.Y + a.Height, b.Y + b.Height);
    return RectF(x1, y1, Max(0.0f, x2 - x1), Max(0.0f, y2 - y1));
}

void AddRound(GraphicsPath& p, const RectF& r, float rad) {
    float d = Min(rad * 2, Min(r.Width, r.Height));
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    AddRound(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void StrokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w, bool dashed) {
    GraphicsPath p;
    AddRound(p, r, rad);
    Pen pen(c, w);
    if (dashed) pen.SetDashStyle(DashStyleDash);
    g.DrawPath(&pen, &p);
}

void FillCircle(Graphics& g, float cx, float cy, float r, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}

void StrokeCircle(Graphics& g, float cx, float cy, float r, const Color& c, float w) {
    Pen pen(c, w);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);
}

void DrawArc(Graphics& g, float cx, float cy, float r, float start, float sweep, const Color& c, float w) {
    Pen pen(c, w);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawArc(&pen, cx - r, cy - r, r * 2, r * 2, start, sweep);
}

void Text(Graphics& g, const std::wstring& s, Font* f, const RectF& r, const Color& c, StringAlignment h,
          StringAlignment v) {
    // Typographic format (no extra padding) so layout matches MeasureText().
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, r, &sf, &b);
}

float WrappedText(Graphics& g, const std::wstring& s, Font* f, float x, float y, float w, const Color& c,
                  StringAlignment h) {
    StringFormat sf;
    sf.SetAlignment(h);
    RectF bounds;
    g.MeasureString(s.c_str(), -1, f, RectF(x, y, w, 10000.0f), &sf, &bounds);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, RectF(x, y, w, bounds.Height + 2), &sf, &b);
    return bounds.Height;
}

float MeasureText(Graphics& g, const std::wstring& s, Font* f) {
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    RectF out;
    g.MeasureString(s.c_str(), -1, f, PointF(0, 0), &sf, &out);
    return out.Width;
}

void AddHit(const RectF& r, int action, int param) {
    if (r.Width > 0 && r.Height > 0) hits.push_back({r, action, param});
}

const Hit* HitAt(float x, float y) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (Contains(it->r, x, y)) return &*it;
    return nullptr;
}

void Button(Graphics& g, const RectF& r, const std::wstring& label, int action, int param, Btn style, float fontPt,
            bool enabled) {
    bool hot = enabled && IsHot(action, param), pressed = enabled && IsPressed(action, param);
    float rad = S(8);
    Color fg = Hex(theme::text);
    switch (style) {
    case Btn::Primary:
        FillRound(g, r, rad, Hex(hot && !pressed ? theme::accentHi : theme::accent, pressed ? 200 : (enabled ? 255 : 90)));
        fg = Color(enabled ? 255 : 150, 255, 255, 255);
        break;
    case Btn::Secondary:
        FillRound(g, r, rad, Hex(hot ? theme::cardHover : theme::card));
        StrokeRound(g, r, rad, Hex(theme::border), S(1));
        if (!enabled) fg = Hex(theme::faint);
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
    Text(g, label, F(fontPt, true), r, fg, StringAlignmentCenter, StringAlignmentCenter);
    if (enabled) AddHit(r, action, param);
}

void SectionLabel(Graphics& g, float x, float y, float w, const std::wstring& s, const std::wstring& right) {
    Text(g, s, F(10.5f, true), RectF(x, y, w, S(16)), Hex(theme::faint));
    if (!right.empty()) Text(g, right, F(10.5f, true), RectF(x, y, w, S(16)), Hex(theme::faint), StringAlignmentFar);
}

float Chips(Graphics& g, float x, float y, float w, const std::vector<Chip>& chips, const std::wstring& sep) {
    Font* f = F(12.0f);
    float h = S(26), padX = S(10), gap = S(6), dotW = S(12);
    float sepW = sep.empty() ? 0 : MeasureText(g, sep, f) + S(2);
    float cx = x, cy = y;
    for (size_t i = 0; i < chips.size(); ++i) {
        const Chip& c = chips[i];
        float cw = Min(MeasureText(g, c.text, f) + padX * 2 + (c.hasDot ? dotW : 0) + S(4), w);
        float need = cw + (i + 1 < chips.size() ? sepW : 0);
        if (cx > x && cx + need > x + w) { cx = x; cy += h + gap; }
        RectF r(cx, cy, cw, h);
        FillRound(g, r, h / 2, c.bg);
        float tx = r.X + padX;
        if (c.hasDot) {
            FillCircle(g, tx + S(3), r.Y + h / 2, S(3.5f), c.dot);
            tx += dotW;
        }
        Text(g, c.text, f, RectF(tx - S(2), r.Y, r.X + r.Width - tx + S(4), h), c.fg);
        cx += cw;
        if (!sep.empty() && i + 1 < chips.size()) {
            Text(g, sep, f, RectF(cx, cy, sepW + S(2), h), Hex(theme::faint), StringAlignmentCenter);
            cx += sepW;
        }
        cx += gap;
    }
    return chips.empty() ? 0 : (cy - y) + h;
}

}  // namespace ui
