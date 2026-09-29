// UI themes: the original dark look, and "Sketch": graphite pencil outlines and
// coloured-pencil hatching on grained off-white paper. The choice is shared by the
// plugin and SimpleAmp Live and remembered in ~/Library/Application Support/SimpleAmp.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sa_ui
{
//==============================================================================
struct Palette
{
    juce::Colour bg, panel, accent, text, dim, line, ledOff, ledOver, field, fieldLine;
};

inline const Palette& darkPalette()
{
    static const Palette p { juce::Colour (0xff17171a), juce::Colour (0xff232327), juce::Colour (0xffe0572a), juce::Colour (0xffe8e6e3),
                             juce::Colour (0xff8d8a86), juce::Colour (0xff34343a), juce::Colour (0xff38383d), juce::Colour (0xff4a4a50),
                             juce::Colour (0xff1c1c20), juce::Colour (0xff3a3a40) };
    return p;
}

inline const Palette& sketchPalette()
{
    static const Palette p { juce::Colour (0xfff3efe5), juce::Colour (0xffebe6da), juce::Colour (0xffd9542a), juce::Colour (0xff2f3034),
                             juce::Colour (0xff77736c), juce::Colour (0xff3a3b3f), juce::Colour (0xfff3efe5), juce::Colour (0xffe4ded0),
                             juce::Colour (0xfff7f4ec), juce::Colour (0xff3a3b3f) };
    return p;
}

namespace detail
{
inline juce::File lookFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/SimpleAmp/look.txt");
}
inline bool& sketchFlag()
{
    static bool flag = lookFile().loadFileAsString().trim() == "sketch";
    return flag;
}
inline int& version() { static int v = 0; return v; }
} // namespace detail

inline bool isSketch() { return detail::sketchFlag(); }
inline int themeVersion() { return detail::version(); } // changes whenever the look is switched
inline const Palette& pal() { return isSketch() ? sketchPalette() : darkPalette(); }

inline void setSketch (bool on)
{
    if (on == isSketch()) return;
    detail::sketchFlag() = on;
    ++detail::version();
    auto f = detail::lookFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (on ? "sketch" : "dark");
}

// Handwritten face for the sketch look (ships with macOS); the normal face otherwise.
inline juce::String sketchTypeface()
{
    static const juce::String name = []
    {
        const auto all = juce::Font::findAllTypefaceNames();
        for (auto n : { "Noteworthy", "Chalkboard SE", "Bradley Hand" })
            if (all.contains (n)) return juce::String (n);
        return juce::String();
    }();
    return name;
}

inline juce::Font themed (const juce::Font& f)
{
    if (! isSketch() || sketchTypeface().isEmpty()) return f;
    return juce::Font (juce::FontOptions (sketchTypeface(), f.getHeight() * 1.08f, f.isBold() ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font font (float size, bool bold = false)
{
    return themed (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
}

//==============================================================================
namespace sketch
{
inline juce::uint32 seedOf (juce::Rectangle<float> r, juce::uint32 salt = 0)
{
    return (juce::uint32) juce::roundToInt (r.getX() * 73.0f + r.getY() * 151.0f + r.getWidth() * 13.0f + r.getHeight() * 7.0f) * 2654435761u + salt;
}

// Paper grain and pencil "tooth" (specks where pigment misses the paper), both tileable.
struct Textures
{
    juce::Image paper, tooth;

    Textures()
    {
        constexpr int n = 512, grid = 32;
        juce::Random rnd (1234);
        float coarse[grid][grid];
        for (auto& row : coarse) for (auto& v : row) v = rnd.nextFloat() * 2.0f - 1.0f;
        auto smooth = [&] (int x, int y) // bilinear value noise, wraps at the edges
        {
            const float gx = x * (float) grid / n, gy = y * (float) grid / n;
            const int x0 = (int) gx, y0 = (int) gy, x1 = (x0 + 1) % grid, y1 = (y0 + 1) % grid;
            const float fx = gx - x0, fy = gy - y0;
            const float a = coarse[y0][x0] + (coarse[y0][x1] - coarse[y0][x0]) * fx;
            const float b = coarse[y1][x0] + (coarse[y1][x1] - coarse[y1][x0]) * fx;
            return a + (b - a) * fy;
        };

        paper = juce::Image (juce::Image::RGB, n, n, false, juce::SoftwareImageType());
        tooth = juce::Image (juce::Image::ARGB, n, n, true, juce::SoftwareImageType());
        const auto base = sketchPalette().bg;
        juce::Image::BitmapData pd (paper, juce::Image::BitmapData::writeOnly), td (tooth, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float fine = rnd.nextFloat() * 2.0f - 1.0f;
                const float blot = smooth (x, y);
                const float fibre = ((x * 7 + y * 3) % 97 == 0) ? -0.8f : 0.0f; // faint paper fibres
                const float v = fine * 5.0f + blot * 4.0f + fibre * 3.0f;
                pd.setPixelColour (x, y, juce::Colour::fromRGB ((juce::uint8) juce::jlimit (0, 255, (int) (base.getRed() + v)),
                                                                (juce::uint8) juce::jlimit (0, 255, (int) (base.getGreen() + v)),
                                                                (juce::uint8) juce::jlimit (0, 255, (int) (base.getBlue() + v * 1.2f))));
                const float t = 0.6f * rnd.nextFloat() + 0.4f * (blot * 0.5f + 0.5f);
                const float a = juce::jlimit (0.0f, 1.0f, (t - 0.5f) * 2.4f);
                td.setPixelColour (x, y, base.withAlpha (a));
            }
    }
};

inline juce::FillType textureFill (const juce::Image& img, juce::Point<float> origin)
{
    // Half size: one image pixel per screen pixel on Retina, fine grain.
    return juce::FillType (img, juce::AffineTransform::scale (0.5f).translated (-origin.x, -origin.y));
}

// Fills the area with paper. `origin` is where this component sits in the window, so the grain lines up.
inline void paper (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> origin = {})
{
    juce::SharedResourcePointer<Textures> tex;
    g.saveState();
    g.setFillType (textureFill (tex->paper, origin));
    g.fillRect (area);
    g.restoreState();
}

inline juce::Point<float> originIn (const juce::Component& c)
{
    if (auto* top = c.getTopLevelComponent()) return top->getLocalPoint (&c, juce::Point<float>());
    return {};
}

// Paper specks over whatever was just drawn inside `area`.
inline void tooth (juce::Graphics& g, const juce::Path& area, float amount = 1.0f)
{
    juce::SharedResourcePointer<Textures> tex;
    g.saveState();
    g.reduceClipRegion (area);
    g.setFillType (textureFill (tex->tooth, {}));
    g.setOpacity (amount);
    g.fillPath (area);
    g.restoreState();
}

// Redraws a shape as a hand would: slightly wavy, and closed shapes overshoot where the stroke meets itself.
inline juce::Path wobble (const juce::Path& shape, juce::uint32 seed, float amount = 0.8f)
{
    juce::Random rnd ((juce::int64) seed);
    const float f1 = 0.02f + 0.03f * rnd.nextFloat(), f2 = 0.08f + 0.06f * rnd.nextFloat();
    const float p1 = rnd.nextFloat() * 6.28f, p2 = rnd.nextFloat() * 6.28f;

    juce::Path out;
    juce::Array<juce::Point<float>> pts;
    auto flush = [&] (bool closed)
    {
        if (pts.size() < 2) { pts.clear(); return; }
        // Resample every ~4 px, pushing each point sideways by smooth noise.
        juce::Array<juce::Point<float>> res;
        float s = 0;
        for (int i = 1; i < pts.size(); ++i)
        {
            const auto a = pts[i - 1], b = pts[i];
            const float len = a.getDistanceFrom (b);
            if (len <= 0.0f) continue;
            const juce::Point<float> normal ((a.y - b.y) / len, (b.x - a.x) / len);
            const int steps = std::max (1, (int) (len / 4.0f));
            for (int k = 0; k < steps; ++k)
            {
                const float t = (float) k / steps;
                const float off = amount * (0.65f * std::sin (s * f1 + p1) + 0.35f * std::sin (s * f2 + p2));
                res.add (a + (b - a) * t + normal * off);
                s += len / steps;
            }
        }
        res.add (pts.getLast());
        if (closed && res.size() > 4) // carry on a little past the start, drifting outwards
            for (int k = 1; k <= 3; ++k)
            {
                const auto a = res[k - 1], b = res[k];
                const float len = std::max (0.001f, a.getDistanceFrom (b));
                res.add (b + juce::Point<float> ((a.y - b.y) / len, (b.x - a.x) / len) * (0.5f * k * amount));
            }
        out.startNewSubPath (res[0]);
        for (int i = 1; i < res.size(); ++i) out.lineTo (res[i]);
        pts.clear();
    };

    for (juce::PathFlatteningIterator it (shape, {}, 0.3f); it.next();)
    {
        if (pts.isEmpty()) pts.add ({ it.x1, it.y1 });
        pts.add ({ it.x2, it.y2 });
        if (it.closesSubPath) flush (true);
        else if (it.isLastInSubpath()) flush (false);
    }
    flush (false);
    return out;
}

// A graphite line along the shape: a firm stroke plus a lighter second pass, with paper showing through.
inline void outline (juce::Graphics& g, const juce::Path& shape, juce::uint32 seed, float width = 1.3f,
                     juce::Colour colour = sketchPalette().line)
{
    juce::Path strokes;
    juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath (strokes, wobble (shape, seed, 0.7f));
    g.setColour (colour.withMultipliedAlpha (0.85f));
    g.fillPath (strokes);

    juce::Path second;
    juce::PathStrokeType (width * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath (second, wobble (shape, seed * 7919u + 17u, 1.1f));
    g.setColour (colour.withMultipliedAlpha (0.35f));
    g.fillPath (second);

    strokes.addPath (second);
    tooth (g, strokes, 0.55f);
}

inline void box (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::uint32 seed, float width = 1.3f,
                 juce::Colour colour = sketchPalette().line)
{
    juce::Path p;
    p.addRoundedRectangle (r, corner);
    outline (g, p, seed, width, colour);
}

// Coloured-pencil shading: diagonal strokes of varying pressure, clipped to the area.
inline void hatch (juce::Graphics& g, const juce::Path& area, juce::Colour colour, juce::uint32 seed,
                   float strength = 1.0f, float spacing = 2.6f, bool cross = false)
{
    juce::Random rnd ((juce::int64) seed);
    const auto b = area.getBounds().expanded (2.0f);
    g.saveState();
    g.reduceClipRegion (wobble (area, seed + 3u, 0.9f));
    auto pass = [&] (float angleDeg, float alphaScale, float gap)
    {
        const float a = juce::degreesToRadians (angleDeg);
        const juce::Point<float> dir (std::cos (a), -std::sin (a)), across (std::sin (a), std::cos (a));
        const float reach = b.getWidth() + b.getHeight();
        const auto c = b.getCentre();
        for (float t = -reach * 0.5f; t < reach * 0.5f; t += gap * (0.8f + 0.4f * rnd.nextFloat()))
        {
            const auto mid = c + across * t;
            const float len = reach * 0.5f;
            const float alpha = juce::jlimit (0.0f, 1.0f, strength * alphaScale * (0.28f + 0.22f * rnd.nextFloat()));
            g.setColour (colour.withAlpha (alpha));
            const float bend = (rnd.nextFloat() - 0.5f) * 1.2f;
            g.drawLine ({ mid - dir * len + across * bend, mid + dir * len - across * bend }, 1.1f + 0.8f * rnd.nextFloat());
        }
    };
    pass (58.0f, 1.0f, spacing);
    if (cross) pass (-35.0f, 0.7f, spacing * 1.4f);
    g.restoreState();
    tooth (g, area, 0.8f);
}

inline void hatchBox (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour colour, juce::uint32 seed,
                      float strength = 1.0f, bool cross = false)
{
    juce::Path p;
    p.addRoundedRectangle (r, corner);
    hatch (g, p, colour, seed, strength, 2.6f, cross);
}

// Text with the paper's grain in it, as if written with a pencil.
inline void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Justification just,
                  const juce::Font& f, juce::Colour colour)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, s, area.getX(), area.getY(), area.getWidth(), area.getHeight(), just, 1);
    juce::Path p;
    ga.createPath (p);
    g.setColour (colour);
    g.fillPath (p);
    tooth (g, p, 0.7f);
}
} // namespace sketch
} // namespace sa_ui
