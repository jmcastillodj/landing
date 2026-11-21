// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

//==============================================================================
// Every color, font and measurement of the interface is here, so that the look
// can be changed in one place.
//
// The look is the identity of Jm Castillo (jmcastillo.es), in its dark form: ink for the ground, bone white for
// the text and the signal, the red of the brand for what is chosen and for the main signal, one typeface (Inter
// Tight), flat colours and lines of one pixel. Nothing has a gradient, a shadow or a glow. The other colour
// schemes of the first ULTIMATE METER are still there to be chosen.
namespace Theme
{
    // Chrome: the header and the bottom bar
    inline juce::Colour chromeTop { 0xff0a0a0a };
    inline juce::Colour chromeBottom { 0xff0a0a0a };
    inline juce::Colour chromeInsetTop { 0xff0a0a0a };
    inline juce::Colour chromeInsetBottom { 0xff0a0a0a };
    inline juce::Colour footerTop { 0xff0a0a0a };
    inline juce::Colour footerBottom { 0xff0a0a0a };
    inline juce::Colour edge { 0xff262626 };
    inline juce::Colour window { 0xff1f1f1f };

    // Displays
    inline juce::Colour displayTop { 0xff0a0a0a };
    inline juce::Colour displayBottom { 0xff0a0a0a };
    inline juce::Colour display { 0xff0a0a0a };
    inline juce::Colour track { 0xff1c1c1c };
    inline juce::Colour grid { 0xff1e1e1e };
    inline juce::Colour gridStrong { 0xff343434 };

    // Floating panels and menus
    inline juce::Colour panel { 0xff161616 };
    inline juce::Colour panelEdge { 0xff3a3a3a };
    inline juce::Colour menu { 0xff161616 };
    inline juce::Colour menuHighlight { 0xffe01e2d };

    // Text: grey labels in front of pale values
    inline juce::Colour text { 0xfff1f0ee };
    inline juce::Colour textDim { 0xff9a9895 };
    inline juce::Colour textFaint { 0xff62615f };
    inline juce::Colour knobLabel { 0xffc9c7c3 };
    inline juce::Colour wordmarkTop { 0xffffffff };
    inline juce::Colour wordmarkBottom { 0xffffffff };

    // Signal
    inline juce::Colour accent { 0xffe01e2d };
    inline juce::Colour accentDeep { 0xff5c0d13 };
    inline juce::Colour second { 0xfff1f0ee };
    inline juce::Colour secondDeep { 0xff5a5957 };
    inline juce::Colour warn { 0xffe2b23c };

    // The green of a meter, as Studio One's are: deep at the foot of a bar, and brighter towards its top
    inline juce::Colour meterDeep { 0xff6f6e6c };
    inline juce::Colour meterMid { 0xffb9b7b4 };
    inline juce::Colour meterLight { 0xfff1f0ee };
    inline juce::Colour held { 0xffffffff };
    inline juce::Colour target { 0xffd9dee4 };
    inline juce::Colour good { 0xfff1f0ee };
    inline juce::Colour over { 0xffff2d2d };

    //==============================================================================
    // The colour schemes. A scheme is the whole palette above, in the order that it is declared there, and the
    // colours of the interface change to it when it is applied.
    inline const juce::StringArray themeNames { "Jm Castillo", "Studio", "Neon Noir", "Emerald", "Aurora", "Ember" };
    inline int currentTheme = 0;
    inline bool flat = true; // the identity has no shadows and no glows, the older schemes keep theirs
    inline int version = 0; // counts the changes, so that images that were made of the colours can be made again

    inline void apply(int index)
    {
        static const juce::uint32 schemes[][36] = {
        { /* Jm Castillo */ 0xff0a0a0a, 0xff0a0a0a, 0xff0a0a0a, 0xff0a0a0a, 0xff0a0a0a, 0xff0a0a0a, 0xff262626, 0xff1f1f1f, 0xff0a0a0a, 0xff0a0a0a, 0xff0a0a0a, 0xff1c1c1c, 0xff1e1e1e, 0xff343434, 0xff161616, 0xff3a3a3a, 0xff161616, 0xffe01e2d, 0xfff1f0ee, 0xff9a9895, 0xff62615f, 0xffc9c7c3, 0xffffffff, 0xffffffff, 0xffe01e2d, 0xff5c0d13, 0xfff1f0ee, 0xff5a5957, 0xffe2b23c, 0xff6f6e6c, 0xffb9b7b4, 0xfff1f0ee, 0xffffffff, 0xffd9dee4, 0xfff1f0ee, 0xffff2d2d },
        { /* Studio */ 0xff40444b, 0xff2f3237, 0xff202226, 0xff17181b, 0xff34373d, 0xff2a2c31, 0xff121316, 0xff0d0e10, 0xff15171b, 0xff1d2026, 0xff181b1f, 0xff23262c, 0xff272b32, 0xff3a3f48, 0xff2b2e34, 0xff535963, 0xff2b2e34, 0xff2a78c8, 0xffdde0e5, 0xff9298a2, 0xff606670, 0xffbcc1c9, 0xffffffff, 0xffb7bdc7, 0xff3d9df5, 0xff1d4f82, 0xfff0922b, 0xff6a4216, 0xfff2c82e, 0xff17653a, 0xff22a553, 0xff45d36f, 0xffffffff, 0xff45d36f, 0xff45d36f, 0xffe5483a },
        { /* Neon Noir */ 0xff2b2236, 0xff1d1727, 0xff120e1a, 0xff0b0810, 0xff241c30, 0xff19131f, 0xff07050a, 0xff050308, 0xff0d0a14, 0xff150f1f, 0xff110c19, 0xff1d1629, 0xff231a30, 0xff3a2c4f, 0xff231a30, 0xff5a4678, 0xff1d1527, 0xff7b2fd6, 0xfff1e9ff, 0xffa593c4, 0xff655577, 0xffcdbfe6, 0xffffffff, 0xffc9b6ee, 0xff25e2f5, 0xff0f6a7a, 0xffff3fa4, 0xff7a1c52, 0xffffd84a, 0xff17653a, 0xff22a553, 0xff45d36f, 0xffffffff, 0xff45d36f, 0xff45d36f, 0xffe5483a },
        { /* Emerald */ 0xff22302a, 0xff17211c, 0xff0f1613, 0xff0a0f0d, 0xff1d2923, 0xff151e19, 0xff060a08, 0xff040806, 0xff0b120f, 0xff111b16, 0xff0e1612, 0xff18241e, 0xff1b2a23, 0xff2e4639, 0xff1b2822, 0xff466555, 0xff16211b, 0xff14946a, 0xffe3f1ea, 0xff8fa89b, 0xff58695f, 0xffb7cfc3, 0xffffffff, 0xffb5ccc0, 0xff2ee59d, 0xff126a4c, 0xffe8b64a, 0xff6b5416, 0xfff4d03f, 0xff0f5b50, 0xff1da58e, 0xff4be3c4, 0xffffffff, 0xff4be3c4, 0xff4be3c4, 0xffe5483a },
        { /* Aurora */ 0xff2c2955, 0xff1e1c3f, 0xff14122c, 0xff0c0b1d, 0xff262349, 0xff1a1838, 0xff07071a, 0xff050513, 0xff0f0e25, 0xff17153a, 0xff121130, 0xff1f1d45, 0xff25224d, 0xff3d3a78, 0xff25224d, 0xff5b58a8, 0xff1d1b40, 0xff5b4fe0, 0xffe9e7ff, 0xff9f9bd0, 0xff5e5b92, 0xffc9c6ee, 0xffffffff, 0xffbfbbee, 0xff8f7dff, 0xff3d3699, 0xffff6fc9, 0xff7d2f60, 0xffffd166, 0xff17653a, 0xff22a553, 0xff45d36f, 0xffffffff, 0xff45d36f, 0xff45d36f, 0xffe5483a },
        { /* Ember */ 0xff3a2f2b, 0xff2a2220, 0xff1c1614, 0xff120e0d, 0xff332a27, 0xff261f1d, 0xff0f0b0a, 0xff0a0706, 0xff161110, 0xff201918, 0xff1a1413, 0xff2a201e, 0xff2e2321, 0xff4b3a36, 0xff2d2422, 0xff6b5650, 0xff271f1d, 0xffd2551e, 0xfff0e6e1, 0xffaa9a93, 0xff6d5f59, 0xffd1c3bc, 0xffffffff, 0xffcdbdb5, 0xffff8a3d, 0xff8a4210, 0xff35d0c6, 0xff12605b, 0xfff7d13b, 0xff17653a, 0xff22a553, 0xff45d36f, 0xffffffff, 0xff45d36f, 0xff45d36f, 0xffff3d5a },
        };

        index = juce::jlimit(0, (int)(sizeof(schemes) / sizeof(schemes[0])) - 1, index);
        const auto* c = schemes[index];
        juce::Colour* all[] { &chromeTop, &chromeBottom, &chromeInsetTop, &chromeInsetBottom, &footerTop, &footerBottom, &edge, &window, &displayTop, &displayBottom, &display, &track, &grid, &gridStrong, &panel, &panelEdge, &menu, &menuHighlight, &text, &textDim, &textFaint, &knobLabel, &wordmarkTop, &wordmarkBottom, &accent, &accentDeep, &second, &secondDeep, &warn, &meterDeep, &meterMid, &meterLight, &held, &target, &good, &over };

        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
            *all[i] = juce::Colour(c[i]);

        currentTheme = index;
        flat = index == 0;
        ++version;
    }

    // How strongly a curve is filled beneath its line. The second curve usually lies over the first,
    // as left and right do, so its fill is faint, or the two would mix into a muddy color.
    inline constexpr float curveThickness = 2.f;

    // A curve carries its color in its line. Beneath it there is only its light: a fill that is this strong
    // under the top of the curve and has faded out this far down the plot, and a wide, faint stroke for a bloom.
    // Large areas of flat color make a display look heavy, which is why nothing is filled flat.
    inline constexpr float curveLightAlpha = 0.26f;
    inline constexpr float curveLightReach = 0.62f;
    inline constexpr float curveBloomAlpha = 0.13f;
    inline constexpr float curveBloomWidth = 5.5f;

    // A bar is the same: its body is held back, and the line at its top, which is the reading, is bright
    inline constexpr float barBodyAlpha = 0.9f;
    inline constexpr float barCapHeight = 2.f;

    // Measurements in pixels
    inline constexpr int headerHeight = 38;
    inline constexpr int bottomBarHeight = 26;
    inline constexpr int sideColumnWidth = 244;
    inline constexpr int gap = 1;

    // The readouts that are numbers change no more often than this, because faster cannot be read
    inline constexpr double readoutIntervalSeconds = 0.1;

    // Inter Tight, the one typeface of the identity, comes inside the plugin. The editor holds a Fonts for as
    // long as it lives, and the typefaces are let go with the last of them.
    inline juce::Typeface::Ptr regularTypeface, mediumTypeface;
    inline int fontUsers = 0;

    struct Fonts
    {
        Fonts()
        {
            if (fontUsers++ == 0)
            {
                regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::InterTightRegular_ttf, BinaryData::InterTightRegular_ttfSize);
                mediumTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::InterTightMedium_ttf, BinaryData::InterTightMedium_ttfSize);
            }
        }

        ~Fonts()
        {
            if (--fontUsers == 0)
                regularTypeface = mediumTypeface = nullptr;
        }
    };

    // The heavier weight is Medium: in the identity the strength comes from size and capitals, not from bold
    inline juce::Font font(float height, bool bold = false)
    {
        if (auto typeface = bold ? mediumTypeface : regularTypeface)
            return juce::Font(juce::FontOptions(typeface).withHeight(height));

        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Small capitals with a little space between the letters, for the names of things
    inline juce::Font labelFont()   { return font(10.5f, true).withExtraKerningFactor(0.06f); }
    inline juce::Font controlFont() { return font(12.5f); }

    // The colors of a level from the foot of a scale to its top: green through the working range,
    // brighter as it rises, yellow from yellowFromDb, and red from redFromDb. The level bars, the history of
    // the levels and the loudness bars all use it, the last with the target in place of full scale.
    inline juce::ColourGradient levelColours(float minDb, float maxDb, float yellowFromDb, float redFromDb,
                                             juce::Point<float> foot, juce::Point<float> top)
    {
        auto proportionOf = [&](float decibels) { return (double)juce::jlimit(0.f, 1.f, juce::jmap(decibels, minDb, maxDb, 0.f, 1.f)); };

        juce::ColourGradient gradient(meterDeep, foot, over, top, false);
        gradient.addColour(proportionOf(yellowFromDb - 24.f), meterMid);
        gradient.addColour(proportionOf(yellowFromDb - 5.f), meterLight);
        gradient.addColour(proportionOf(yellowFromDb), warn);
        gradient.addColour(proportionOf(redFromDb), over);
        return gradient;
    }

    inline int textWidth(const juce::Font& f, const juce::String& t)
    {
        return juce::GlyphArrangement::getStringWidthInt(f, t);
    }

    // Fills a display with its background, which is given the height of the whole display
    // so that views that draw it in parts stay in step
    inline void fillDisplay(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setGradientFill(juce::ColourGradient(displayTop, 0.f, (float)bounds.getY() + 0.25f * (float)bounds.getHeight(),
                                               displayBottom, 0.f, (float)bounds.getBottom(), false));
        g.fillRect(bounds);
    }

    // Draws a floating panel: dark and rounded, with a lighter edge and a soft shadow under it
    inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> bounds, float alpha = 0.94f)
    {
        juce::Path outline;
        outline.addRoundedRectangle(bounds, 6.f);

        if (!flat)
            juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 12, { 0, 4 }).drawForPath(g, outline);
        g.setColour(panel.withAlpha(alpha));
        g.fillPath(outline);
        g.setColour(panelEdge);
        g.strokePath(outline, juce::PathStrokeType(1.f));
    }

    // Formats a level in decibels with one decimal place, a true minus sign, and a dash for silence
    inline juce::String formatDb(float decibels, float silenceBelow = -100.f)
    {
        if (!(decibels > silenceBelow))
            return juce::String(juce::CharPointer_UTF8("\xe2\x80\x93"));

        const auto number = juce::String(std::abs(decibels), 1);
        return decibels < -0.05f ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + number
             : decibels > 0.05f ? "+" + number
             : juce::String("0.0");
    }

    // Formats a frequency as it is spoken: 440 Hz, 1.25 kHz
    inline juce::String formatFrequency(double hertz)
    {
        return hertz < 1000.0 ? juce::String(juce::roundToInt(hertz)) + " Hz"
                              : juce::String(hertz / 1000.0, hertz < 10000.0 ? 2 : 1) + " kHz";
    }

    // Names the note nearest to a frequency, with how far off it is in cents: A4, C#3 +12c
    inline juce::String formatNote(double hertz)
    {
        if (hertz <= 0.0)
            return {};

        static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const double midi = 69.0 + 12.0 * std::log2(hertz / 440.0);
        const int nearest = juce::roundToInt(midi);
        const int cents = juce::roundToInt((midi - nearest) * 100.0);

        juce::String note = juce::String(names[((nearest % 12) + 12) % 12]) + juce::String(nearest / 12 - 1);
        if (cents != 0)
            note << (cents > 0 ? " +" : " ") << cents << "c";

        return note;
    }
}
