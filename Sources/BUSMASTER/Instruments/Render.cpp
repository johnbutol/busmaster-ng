#include "stdafx.h"
#include "Render.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

using namespace Gdiplus;

namespace Instruments
{
namespace
{
const double kPi = 3.14159265358979323846;
const float kGaugeStart = 135.0f;       // degrees, clockwise from the x axis (y down): bottom left
const float kGaugeSweep = 270.0f;
const int kUpDownButtonWidth = 18;

Color ToColor(COLORREF color, BYTE alpha = 255)
{
    return Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

bool PropColor(const Control& control, const char* name, Color& color)
{
    unsigned long value;
    if (ParseColor(control.Get(name), value))
    {
        color = ToColor(value);
        return true;
    }
    return false;
}

Color PropColorOr(const Control& control, const char* name, COLORREF fallback)
{
    Color color;
    return PropColor(control, name, color) ? color : ToColor(fallback);
}

Color Shade(const Color& color, float factor)
{
    auto channel = [factor](BYTE value)
    {
        float result = factor >= 1.0f ? value + (255 - value) * (factor - 1.0f) : value * factor;
        return (BYTE)std::max(0.0f, std::min(255.0f, result));
    };
    return Color(color.GetA(), channel(color.GetR()), channel(color.GetG()), channel(color.GetB()));
}

std::unique_ptr<Gdiplus::Font> MakeFont(const Control& control, float scale = 1.0f, int style = FontStyleRegular)
{
    float size = (float)control.GetInt("FontSize");
    if (size <= 0)
    {
        size = 9;
    }
    return std::make_unique<Gdiplus::Font>(L"Segoe UI", size * scale, style, UnitPoint);
}

void FillBackground(Graphics& g, const Control& control, const RectF& rect)
{
    Color back;
    if (PropColor(control, "BackColor", back))
    {
        SolidBrush brush(back);
        g.FillRectangle(&brush, rect);
    }
}

void RoundRect(GraphicsPath& path, const RectF& rect, float radius)
{
    float d = std::min(radius * 2, std::min(rect.Width, rect.Height));
    if (d <= 1)
    {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void DrawText(Graphics& g, const std::string& text, const Gdiplus::Font& font, const RectF& rect, const Color& color,
              StringAlignment horizontal = StringAlignmentNear, StringAlignment vertical = StringAlignmentCenter,
              bool wrap = false)
{
    if (text.empty())
    {
        return;
    }
    StringFormat format;
    format.SetAlignment(horizontal);
    format.SetLineAlignment(vertical);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap)
    {
        format.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    SolidBrush brush(color);
    std::wstring wide = Widen(text);
    g.DrawString(wide.c_str(), -1, &font, rect, &format, &brush);
}

RectF Bounds(const Control& control)
{
    return RectF((REAL)control.X(), (REAL)control.Y(), (REAL)control.W(), (REAL)control.H());
}

// ---- pictures: decoded once, kept by content
std::map<size_t, std::shared_ptr<Bitmap>> cache;

std::shared_ptr<Bitmap> PictureOf(const std::string& base64)
{
    if (base64.empty())
    {
        return nullptr;
    }
    size_t key = std::hash<std::string>()(base64);
    auto it = cache.find(key);
    if (it != cache.end())
    {
        return it->second;
    }
    std::shared_ptr<Bitmap> result;
    std::vector<unsigned char> data = Base64Decode(base64);
    IStream* stream = SHCreateMemStream(data.data(), (UINT)data.size());
    if (stream != nullptr)
    {
        Bitmap* loaded = Bitmap::FromStream(stream);
        if (loaded != nullptr && loaded->GetLastStatus() == Ok)
        {
            // a copy that does not need the stream any more
            Bitmap* copy = new Bitmap(loaded->GetWidth(), loaded->GetHeight(), PixelFormat32bppARGB);
            {
                Graphics cg(copy);
                cg.DrawImage(loaded, 0, 0, loaded->GetWidth(), loaded->GetHeight());
            }
            result.reset(copy);
        }
        delete loaded;
        stream->Release();
    }
    if (cache.size() > 64)
    {
        cache.clear();
    }
    cache[key] = result;
    return result;
}

// ---- the controls
void DrawGauge(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    float size = std::min(r.Width, r.Height);
    if (size < 20)
    {
        return;
    }
    PointF center(r.X + r.Width / 2, r.Y + r.Height / 2);
    float radius = size / 2 - 2;
    Color fore = PropColorOr(c, "ForeColor", RGB(0, 0, 0));

    // dial: white unless the control has a color of its own, which then fills the whole control
    Color dialColor(255, 255, 255, 255);
    PropColor(c, "BackColor", dialColor);
    SolidBrush dial(dialColor);
    g.FillEllipse(&dial, center.X - radius, center.Y - radius, radius * 2, radius * 2);
    Pen rim(Color(255, 120, 120, 120), std::max(1.0f, radius * 0.02f));
    g.DrawEllipse(&rim, center.X - radius, center.Y - radius, radius * 2, radius * 2);

    double minimum = c.GetNumber("Minimum");
    double maximum = c.GetNumber("Maximum");
    if (maximum <= minimum)
    {
        maximum = minimum + 1;
    }
    auto angleOf = [&](double value)
    {
        double f = (value - minimum) / (maximum - minimum);
        f = std::max(-0.02, std::min(1.02, f));
        return kGaugeStart + kGaugeSweep * (float)f;
    };
    auto pointAt = [&](float angle, float distance)
    {
        double a = angle * kPi / 180.0;
        return PointF(center.X + distance * (float)cos(a), center.Y + distance * (float)sin(a));
    };

    // ranges
    if (c.GetBool("ShowRanges"))
    {
        double lower = std::max(minimum, std::min(maximum, c.GetNumber("LowerRange")));
        double upper = std::max(lower, std::min(maximum, c.GetNumber("UpperRange")));
        float arcRadius = radius * 0.86f;
        float width = std::max(2.0f, radius * 0.07f);
        RectF arc(center.X - arcRadius, center.Y - arcRadius, arcRadius * 2, arcRadius * 2);
        struct Band
        {
            double from, to;
            Color color;
        } bands[] =
        {
            { minimum, lower, Color(255, 40, 170, 60) },
            { lower, upper, Color(255, 240, 200, 0) },
            { upper, maximum, Color(255, 220, 40, 30) },
        };
        for (auto& band : bands)
        {
            if (band.to > band.from)
            {
                Pen pen(band.color, width);
                float a0 = angleOf(band.from), a1 = angleOf(band.to);
                g.DrawArc(&pen, arc, a0, a1 - a0);
            }
        }
    }

    // scale
    double step = c.GetNumber("MajorStep");
    int minor = std::max(0, std::min(20, c.GetInt("MinorTicks")));
    if (step <= 0 || (maximum - minimum) / step > 100)
    {
        step = (maximum - minimum) / 10;
    }
    Pen major(fore, std::max(1.0f, radius * 0.025f));
    Pen minorPen(fore, std::max(1.0f, radius * 0.01f));
    auto font = MakeFont(c, std::max(0.5f, radius / 120.0f));
    int count = (int)floor((maximum - minimum) / step + 1e-6);
    for (int i = 0; i <= count; i++)
    {
        double value = minimum + i * step;
        float a = angleOf(value);
        g.DrawLine(&major, pointAt(a, radius * 0.93f), pointAt(a, radius * 0.78f));
        if (i < count)
        {
            for (int k = 1; k <= minor; k++)
            {
                float am = angleOf(value + step * k / (minor + 1));
                g.DrawLine(&minorPen, pointAt(am, radius * 0.93f), pointAt(am, radius * 0.85f));
            }
        }
        std::string label = FormatNumber(fabs(value) < 1e-9 ? 0.0 : value);
        PointF p = pointAt(a, radius * 0.64f);
        DrawText(g, label, *font, RectF(p.X - radius * 0.3f, p.Y - radius * 0.1f, radius * 0.6f, radius * 0.2f), fore,
                 StringAlignmentCenter, StringAlignmentCenter);
    }

    // title and unit
    // title above the hub, unit under it, value in the opening at the bottom
    auto titleFont = MakeFont(c, std::max(0.5f, radius / 100.0f), FontStyleBold);
    auto valueFont = MakeFont(c, std::max(0.6f, radius / 80.0f), FontStyleBold);
    std::string title = c.Get("Text");
    std::string unit = ControlUnit(c, s);
    DrawText(g, title, *titleFont, RectF(center.X - radius * 0.5f, center.Y - radius * 0.44f, radius, radius * 0.18f),
             fore, StringAlignmentCenter, StringAlignmentCenter);
    DrawText(g, unit, *font, RectF(center.X - radius * 0.5f, center.Y + radius * 0.14f, radius, radius * 0.16f),
             Color(255, 90, 90, 90), StringAlignmentCenter, StringAlignmentCenter);
    if (c.GetBool("ShowValue"))
    {
        std::string text = s.mHasValue ? FormatValue(c, s, false) : "---";
        RectF box(center.X - radius * 0.34f, center.Y + radius * 0.5f, radius * 0.68f, radius * 0.24f);
        SolidBrush boxBrush(Color(255, 236, 236, 236));
        g.FillRectangle(&boxBrush, box);
        DrawText(g, text, *valueFont, box, fore, StringAlignmentCenter, StringAlignmentCenter);
    }

    // needle
    float a = angleOf(s.mHasValue ? s.mValue : minimum);
    Color needle = PropColorOr(c, "NeedleColor", RGB(192, 0, 0));
    PointF tip = pointAt(a, radius * 0.82f);
    PointF tail = pointAt(a + 180, radius * 0.12f);
    PointF side1 = pointAt(a + 90, radius * 0.035f);
    PointF side2 = pointAt(a - 90, radius * 0.035f);
    PointF points[] = { tip, side1, tail, side2 };
    SolidBrush needleBrush(needle);
    g.FillPolygon(&needleBrush, points, 4);
    SolidBrush hub(Color(255, 60, 60, 60));
    float hubRadius = radius * 0.07f;
    g.FillEllipse(&hub, center.X - hubRadius, center.Y - hubRadius, hubRadius * 2, hubRadius * 2);
}

void DrawButton(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    r.Width -= 1;
    r.Height -= 1;
    bool down = s.mPressed;
    if (c.Get("Mode") == "Toggle" && s.mHasValue)
    {
        down = down || fabs(s.mValue - c.GetNumber("PressedValue")) < 1e-9;
    }
    Color face;
    if (!PropColor(c, "BackColor", face))
    {
        face = Color(255, 225, 225, 225);
    }
    if (s.mHover && !down)
    {
        face = Shade(face, 1.08f);
    }
    GraphicsPath path;
    RoundRect(path, r, 3);
    LinearGradientBrush brush(r, down ? Shade(face, 0.8f) : Shade(face, 1.1f), down ? Shade(face, 0.9f) : Shade(face, 0.92f),
                              LinearGradientModeVertical);
    g.FillPath(&brush, &path);
    Pen border(down ? Color(255, 60, 110, 180) : Color(255, 130, 130, 130), 1);
    g.DrawPath(&border, &path);
    auto font = MakeFont(c);
    RectF text = r;
    if (down)
    {
        text.Offset(1, 1);
    }
    DrawText(g, c.Get("Text"), *font, text, PropColorOr(c, "ForeColor", RGB(0, 0, 0)), StringAlignmentCenter);
}

void DrawBox(Graphics& g, const RectF& r)
{
    SolidBrush white(Color(255, 255, 255, 255));
    g.FillRectangle(&white, r);
    Pen border(Color(255, 122, 122, 122), 1);
    g.DrawRectangle(&border, r.X, r.Y, r.Width - 1, r.Height - 1);
}

void DrawTextBox(Graphics& g, const Control& c, const ControlState& s, bool design)
{
    RectF r = Bounds(c);
    Color back;
    if (PropColor(c, "BackColor", back))
    {
        SolidBrush brush(back);
        g.FillRectangle(&brush, r);
        Pen border(Color(255, 122, 122, 122), 1);
        g.DrawRectangle(&border, r.X, r.Y, r.Width - 1, r.Height - 1);
    }
    else
    {
        DrawBox(g, r);
    }
    std::string text = s.mHasValue ? FormatValue(c, s, c.GetBool("ReadOnly")) : c.Get("Text");
    Color fore = PropColorOr(c, "ForeColor", RGB(0, 0, 0));
    if (text.empty() && design)
    {
        text = c.Get("Name");
        fore = Color(255, 160, 160, 160);
    }
    auto font = MakeFont(c);
    RectF inner(r.X + 4, r.Y, r.Width - 8, r.Height);
    DrawText(g, text, *font, inner, fore);
}

void DrawCheckBox(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    float box = std::min(14.0f, r.Height - 2);
    RectF square(r.X + 1, r.Y + (r.Height - box) / 2, box, box);
    DrawBox(g, square);
    bool checked = s.mHasValue && fabs(s.mValue - c.GetNumber("CheckedValue")) < 1e-9;
    if (checked)
    {
        Pen tick(Color(255, 30, 30, 30), std::max(1.5f, box / 7));
        PointF points[] =
        {
            PointF(square.X + box * 0.2f, square.Y + box * 0.52f),
            PointF(square.X + box * 0.42f, square.Y + box * 0.74f),
            PointF(square.X + box * 0.8f, square.Y + box * 0.26f),
        };
        g.DrawLines(&tick, points, 3);
    }
    if (s.mHover)
    {
        Pen hover(Color(255, 60, 110, 180), 1);
        g.DrawRectangle(&hover, square.X, square.Y, square.Width - 1, square.Height - 1);
    }
    auto font = MakeFont(c);
    DrawText(g, c.Get("Text"), *font, RectF(square.GetRight() + 5, r.Y, r.Width - box - 6, r.Height),
             PropColorOr(c, "ForeColor", RGB(0, 0, 0)));
}

void DrawGroupBox(Graphics& g, const Control& c)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    auto font = MakeFont(c);
    std::wstring text = Widen(c.Get("Text"));
    RectF measured;
    g.MeasureString(text.c_str(), -1, font.get(), PointF(0, 0), &measured);
    float top = r.Y + measured.Height / 2;
    Pen border(Color(255, 170, 170, 170), 1);
    GraphicsPath path;
    RoundRect(path, RectF(r.X, top, r.Width - 1, r.GetBottom() - top - 1), 3);
    // the caption cuts the line
    Region clip(r);
    if (!text.empty())
    {
        clip.Exclude(RectF(r.X + 6, r.Y, measured.Width + 2, measured.Height));
    }
    g.SetClip(&clip);
    g.DrawPath(&border, &path);
    g.ResetClip();
    DrawText(g, c.Get("Text"), *font, RectF(r.X + 7, r.Y, r.Width - 14, measured.Height),
             PropColorOr(c, "ForeColor", RGB(0, 0, 0)));
}

void DrawLabel(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    std::string text = c.Get("Text");
    if (!c.Get("Variable").empty())
    {
        std::string value = s.mHasValue ? FormatValue(c, s, true) : "---";
        text = text.empty() ? value : text + " " + value;
    }
    std::string align = c.Get("Alignment");
    StringAlignment horizontal = align == "Center" ? StringAlignmentCenter : align == "Right" ? StringAlignmentFar : StringAlignmentNear;
    auto font = MakeFont(c);
    DrawText(g, text, *font, r, PropColorOr(c, "ForeColor", RGB(0, 0, 0)), horizontal, StringAlignmentCenter, true);
}

void DrawLed(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    std::string text = c.Get("Text");
    float d = text.empty() ? std::min(r.Width, r.Height) - 2 : r.Height - 2;
    d = std::max(4.0f, d);
    RectF led(text.empty() ? r.X + (r.Width - d) / 2 : r.X + 1, r.Y + (r.Height - d) / 2, d, d);
    bool on = s.mHasValue && s.mValue >= c.GetNumber("Threshold");
    Color color = on ? PropColorOr(c, "OnColor", RGB(0, 208, 0)) : PropColorOr(c, "OffColor", RGB(64, 80, 64));
    GraphicsPath path;
    if (c.Get("Shape") == "Square")
    {
        RoundRect(path, led, d * 0.12f);
    }
    else
    {
        path.AddEllipse(led);
    }
    PathGradientBrush brush(&path);
    brush.SetCenterColor(on ? Shade(color, 1.6f) : Shade(color, 1.25f));
    brush.SetCenterPoint(PointF(led.X + d * 0.38f, led.Y + d * 0.35f));
    Color surround = on ? color : Shade(color, 0.8f);
    int count = 1;
    brush.SetSurroundColors(&surround, &count);
    g.FillPath(&brush, &path);
    Pen border(Shade(color, 0.5f), std::max(1.0f, d / 16));
    g.DrawPath(&border, &path);
    if (!text.empty())
    {
        auto font = MakeFont(c);
        DrawText(g, text, *font, RectF(led.GetRight() + 5, r.Y, r.Width - d - 6, r.Height),
                 PropColorOr(c, "ForeColor", RGB(0, 0, 0)));
    }
}

void DrawUpDown(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    Color back;
    if (!PropColor(c, "BackColor", back))
    {
        back = Color(255, 255, 255, 255);
    }
    SolidBrush backBrush(back);
    g.FillRectangle(&backBrush, r);
    Pen border(Color(255, 122, 122, 122), 1);
    g.DrawRectangle(&border, r.X, r.Y, r.Width - 1, r.Height - 1);
    float bw = (float)std::min(kUpDownButtonWidth, c.W() / 2);
    RectF up(r.GetRight() - bw - 1, r.Y + 1, bw, (r.Height - 2) / 2);
    RectF down(up.X, up.GetBottom(), bw, r.Height - 2 - up.Height);
    SolidBrush face(Color(255, 232, 232, 232));
    g.FillRectangle(&face, up);
    g.FillRectangle(&face, down);
    Pen line(Color(255, 180, 180, 180), 1);
    g.DrawLine(&line, up.X, r.Y, up.X, r.GetBottom());
    g.DrawLine(&line, up.X, up.GetBottom(), r.GetRight(), up.GetBottom());
    SolidBrush arrow(Color(255, 60, 60, 60));
    float aw = std::min(bw * 0.5f, 8.0f), ah = std::min(up.Height * 0.4f, 4.0f);
    PointF upArrow[] = { PointF(up.X + bw / 2, up.Y + (up.Height - ah) / 2), PointF(up.X + (bw + aw) / 2, up.Y + (up.Height + ah) / 2),
                         PointF(up.X + (bw - aw) / 2, up.Y + (up.Height + ah) / 2)
                       };
    PointF downArrow[] = { PointF(down.X + bw / 2, down.Y + (down.Height + ah) / 2), PointF(down.X + (bw + aw) / 2, down.Y + (down.Height - ah) / 2),
                           PointF(down.X + (bw - aw) / 2, down.Y + (down.Height - ah) / 2)
                         };
    g.FillPolygon(&arrow, upArrow, 3);
    g.FillPolygon(&arrow, downArrow, 3);
    ControlState shown = s;
    if (!shown.mHasValue)
    {
        shown.mHasValue = true;
        shown.mValue = ClampToControl(c, 0.0);
    }
    auto font = MakeFont(c);
    DrawText(g, FormatValue(c, shown, false), *font, RectF(r.X + 4, r.Y, r.Width - bw - 8, r.Height),
             PropColorOr(c, "ForeColor", RGB(0, 0, 0)));
}

void DrawPicture(Graphics& g, const Control& c, bool design)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    auto bitmap = PictureOf(c.Get("Image"));
    if (!bitmap)
    {
        if (design)
        {
            Pen pen(Color(255, 160, 160, 160), 1);
            pen.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&pen, r.X, r.Y, r.Width - 1, r.Height - 1);
            g.DrawLine(&pen, r.X, r.Y, r.GetRight() - 1, r.GetBottom() - 1);
            g.DrawLine(&pen, r.X, r.GetBottom() - 1, r.GetRight() - 1, r.Y);
        }
        return;
    }
    float iw = (float)bitmap->GetWidth(), ih = (float)bitmap->GetHeight();
    std::string mode = c.Get("SizeMode");
    RectF target = r;
    if (mode == "Zoom")
    {
        float scale = std::min(r.Width / iw, r.Height / ih);
        target = RectF(r.X + (r.Width - iw * scale) / 2, r.Y + (r.Height - ih * scale) / 2, iw * scale, ih * scale);
    }
    else if (mode == "Center")
    {
        target = RectF(r.X + (r.Width - iw) / 2, r.Y + (r.Height - ih) / 2, iw, ih);
    }
    g.SetClip(r);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(bitmap.get(), target);
    g.ResetClip();
}

// track bar geometry: the part of the control where the thumb runs
struct TrackGeometry
{
    bool mVertical;
    RectF mTrack;       // the line, from its minimum end to its maximum end
    RectF mValue;       // where the value is written
};

TrackGeometry TrackOf(const Control& c)
{
    RectF r = Bounds(c);
    TrackGeometry t;
    t.mVertical = c.Get("Orientation") == "Vertical";
    bool showValue = c.GetBool("ShowValue");
    const float thumb = 6;
    if (t.mVertical)
    {
        float valueHeight = showValue ? 18.0f : 0.0f;
        t.mTrack = RectF(r.X + r.Width / 2, r.Y + thumb + 2, 0, r.Height - valueHeight - 2 * thumb - 4);
        t.mValue = RectF(r.X, r.GetBottom() - valueHeight, r.Width, valueHeight);
    }
    else
    {
        float valueWidth = showValue ? std::min(80.0f, r.Width * 0.4f) : 0.0f;
        t.mTrack = RectF(r.X + thumb + 2, r.Y + r.Height / 2 - 4, r.Width - valueWidth - 2 * thumb - 4, 0);
        t.mValue = RectF(r.GetRight() - valueWidth, r.Y, valueWidth, r.Height);
    }
    return t;
}

void DrawTrackBar(Graphics& g, const Control& c, const ControlState& s)
{
    RectF r = Bounds(c);
    FillBackground(g, c, r);
    TrackGeometry t = TrackOf(c);
    double minimum = c.GetNumber("Minimum"), maximum = c.GetNumber("Maximum");
    if (maximum <= minimum)
    {
        maximum = minimum + 1;
    }
    double value = s.mHasValue ? s.mValue : minimum;
    float f = (float)std::max(0.0, std::min(1.0, (value - minimum) / (maximum - minimum)));
    Color fore = PropColorOr(c, "ForeColor", RGB(0, 0, 0));
    SolidBrush groove(Color(255, 200, 200, 200));
    SolidBrush filled(Color(255, 60, 120, 200));
    Pen tickPen(Color(255, 120, 120, 120), 1);
    double tickStep = c.GetNumber("TickStep");
    int ticks = tickStep > 0 ? (int)floor((maximum - minimum) / tickStep + 1e-6) : 0;
    if (ticks > 200)
    {
        ticks = 0;
    }
    PointF thumbCenter;
    if (t.mVertical)
    {
        float length = t.mTrack.Height;
        float x = t.mTrack.X;
        // the minimum at the bottom
        float y0 = t.mTrack.GetBottom();
        g.FillRectangle(&groove, x - 2, t.mTrack.Y, 4.0f, length);
        g.FillRectangle(&filled, x - 2, y0 - length * f, 4.0f, length * f);
        for (int i = 0; i <= ticks; i++)
        {
            float y = y0 - length * (float)(i * tickStep / (maximum - minimum));
            g.DrawLine(&tickPen, x + 9, y, x + 13, y);
        }
        thumbCenter = PointF(x, y0 - length * f);
        RectF thumb(thumbCenter.X - 10, thumbCenter.Y - 5, 20, 10);
        GraphicsPath path;
        RoundRect(path, thumb, 2);
        LinearGradientBrush brush(thumb, Color(255, 250, 250, 250), Color(255, 210, 210, 210), LinearGradientModeVertical);
        g.FillPath(&brush, &path);
        Pen border(s.mHover || s.mPressed ? Color(255, 60, 110, 180) : Color(255, 120, 120, 120), 1);
        g.DrawPath(&border, &path);
    }
    else
    {
        float length = t.mTrack.Width;
        float y = t.mTrack.Y;
        g.FillRectangle(&groove, t.mTrack.X, y - 2, length, 4.0f);
        g.FillRectangle(&filled, t.mTrack.X, y - 2, length * f, 4.0f);
        for (int i = 0; i <= ticks; i++)
        {
            float x = t.mTrack.X + length * (float)(i * tickStep / (maximum - minimum));
            g.DrawLine(&tickPen, x, y + 9, x, y + 13);
        }
        thumbCenter = PointF(t.mTrack.X + length * f, y);
        RectF thumb(thumbCenter.X - 5, thumbCenter.Y - 10, 10, 20);
        GraphicsPath path;
        RoundRect(path, thumb, 2);
        LinearGradientBrush brush(thumb, Color(255, 250, 250, 250), Color(255, 210, 210, 210), LinearGradientModeVertical);
        g.FillPath(&brush, &path);
        Pen border(s.mHover || s.mPressed ? Color(255, 60, 110, 180) : Color(255, 120, 120, 120), 1);
        g.DrawPath(&border, &path);
    }
    if (c.GetBool("ShowValue") && t.mValue.Width > 0 && t.mValue.Height > 0)
    {
        ControlState shown = s;
        shown.mHasValue = true;
        shown.mValue = value;
        auto font = MakeFont(c);
        DrawText(g, FormatValue(c, shown, true), *font, t.mValue, fore, StringAlignmentCenter);
    }
}
}

void ClearPictureCache()
{
    cache.clear();
}

std::wstring Widen(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), &result[0], size);
    return result;
}

std::string ControlUnit(const Control& control, const ControlState& state)
{
    if (control.Get("ValueType") == "Raw")
    {
        return control.Get("Unit");
    }
    std::string unit = control.Get("Unit");
    return unit.empty() ? state.mUnit : unit;
}

std::string FormatValue(const Control& control, const ControlState& state, bool withUnit)
{
    int decimals = std::max(0, std::min(10, control.GetInt("Decimals")));
    std::string result = FormatNumber(state.mValue, control.Get("ValueType") == "Raw" ? 0 : decimals);
    if (withUnit)
    {
        std::string unit = ControlUnit(control, state);
        if (!unit.empty())
        {
            result += " " + unit;
        }
    }
    return result;
}

double ClampToControl(const Control& control, double value)
{
    double minimum = control.GetNumber("Minimum");
    double maximum = control.GetNumber("Maximum");
    double step = control.GetNumber("Step");
    if (step > 0)
    {
        value = minimum + floor((value - minimum) / step + 0.5) * step;
    }
    if (maximum > minimum)
    {
        value = std::max(minimum, std::min(maximum, value));
    }
    return value;
}

double TrackBarValueAt(const Control& control, int x, int y)
{
    TrackGeometry t = TrackOf(control);
    double minimum = control.GetNumber("Minimum"), maximum = control.GetNumber("Maximum");
    double f;
    if (t.mVertical)
    {
        f = t.mTrack.Height > 0 ? (t.mTrack.GetBottom() - y) / t.mTrack.Height : 0;
    }
    else
    {
        f = t.mTrack.Width > 0 ? (x - t.mTrack.X) / t.mTrack.Width : 0;
    }
    f = std::max(0.0, std::min(1.0, f));
    return ClampToControl(control, minimum + f * (maximum - minimum));
}

int UpDownArrowAt(const Control& control, int x, int y)
{
    int bw = std::min(kUpDownButtonWidth, control.W() / 2);
    if (x < control.X() + control.W() - bw - 1 || x >= control.X() + control.W() || y < control.Y() || y >= control.Y() + control.H())
    {
        return 0;
    }
    return y < control.Y() + control.H() / 2 ? 1 : -1;
}

void DrawControl(Graphics& graphics, const Control& control, const ControlState& state, bool design)
{
    switch (control.mType)
    {
        case ControlType::AnalogGauge:
            DrawGauge(graphics, control, state);
            break;
        case ControlType::Button:
            DrawButton(graphics, control, state);
            break;
        case ControlType::TextBox:
            DrawTextBox(graphics, control, state, design);
            break;
        case ControlType::CheckBox:
            DrawCheckBox(graphics, control, state);
            break;
        case ControlType::GroupBox:
            DrawGroupBox(graphics, control);
            break;
        case ControlType::Label:
            DrawLabel(graphics, control, state);
            break;
        case ControlType::Led:
            DrawLed(graphics, control, state);
            break;
        case ControlType::NumericUpDown:
            DrawUpDown(graphics, control, state);
            break;
        case ControlType::Picture:
            DrawPicture(graphics, control, design);
            break;
        case ControlType::TrackBar:
            DrawTrackBar(graphics, control, state);
            break;
        default:
            break;
    }
}

void DrawPanelBackground(Graphics& graphics, const Instrument& instrument, int width, int height)
{
    unsigned long color;
    if (!ParseColor(instrument.GetPanel("BackColor"), color))
    {
        color = GetSysColor(COLOR_BTNFACE);
    }
    SolidBrush brush(ToColor(color));
    graphics.FillRectangle(&brush, 0, 0, width, height);
}

void DrawToolIcon(Graphics& g, ControlType type, float x, float y, float size)
{
    Control c(type);
    c.SetRect(0, 0, (int)size, (int)size);
    ControlState s;
    s.mHasValue = true;
    GraphicsState saved = g.Save();
    g.TranslateTransform(x, y);
    switch (type)
    {
        case ControlType::AnalogGauge:
            c.Set("ShowValue", "false");
            c.Set("MinorTicks", "0");
            c.Set("MajorStep", "50");
            c.Set("FontSize", "1");
            s.mValue = 70;
            break;
        case ControlType::Button:
            c.Set("Text", "");
            c.SetRect(0, (int)(size * 0.2f), (int)size, (int)(size * 0.6f));
            break;
        case ControlType::TextBox:
            c.Set("Text", "ab");
            c.Set("FontSize", "7");
            c.SetRect(0, (int)(size * 0.2f), (int)size, (int)(size * 0.6f));
            s.mHasValue = false;
            break;
        case ControlType::CheckBox:
            c.Set("Text", "");
            s.mValue = 1;
            break;
        case ControlType::GroupBox:
            c.Set("Text", "");
            c.SetRect(0, (int)(size * 0.1f), (int)size, (int)(size * 0.8f));
            break;
        case ControlType::Label:
            c.Set("Text", "A");
            c.Set("FontSize", "11");
            c.Set("Alignment", "Center");
            break;
        case ControlType::Led:
            s.mValue = 1;
            break;
        case ControlType::NumericUpDown:
            c.Set("FontSize", "7");
            c.SetRect(0, (int)(size * 0.2f), (int)size, (int)(size * 0.6f));
            s.mValue = 5;
            break;
        case ControlType::Picture:
        {
            g.Restore(saved);
            SolidBrush sky(Color(255, 150, 200, 240));
            SolidBrush hill(Color(255, 70, 160, 70));
            SolidBrush sun(Color(255, 250, 210, 40));
            g.FillRectangle(&sky, x + 1, y + size * 0.15f, size - 2, size * 0.7f);
            PointF points[] = { PointF(x + 1, y + size * 0.85f), PointF(x + size * 0.4f, y + size * 0.45f), PointF(x + size - 1, y + size * 0.85f) };
            g.FillPolygon(&hill, points, 3);
            g.FillEllipse(&sun, x + size * 0.62f, y + size * 0.22f, size * 0.2f, size * 0.2f);
            return;
        }
        case ControlType::TrackBar:
            c.Set("ShowValue", "false");
            c.Set("TickStep", "25");
            s.mValue = 60;
            break;
        default:
            break;
    }
    DrawControl(g, c, s, false);
    g.Restore(saved);
}
}
