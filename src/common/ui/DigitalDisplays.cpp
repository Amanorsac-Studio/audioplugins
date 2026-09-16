#include "DigitalDisplays.h"

#include "common/audio/PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
#include <deque>

namespace amanorsac
{
namespace
{
using juce::Colour;

const Colour ink { 0xff0a0d12 }, gridLine { 0xff171c24 }, gridStrong { 0xff222833 };
const Colour textMain { 0xffe9edf2 }, textDim { 0xff7f8894 };
const Colour blue { 0xff2f8bff }, green { 0xff2fd36b }, purple { 0xffa65cf2 }, orange { 0xffff7a1a };
const Colour teal { 0xff2bc4c4 }, red { 0xffef4b4b }, yellow { 0xfff3c832 }, pink { 0xffff5fa2 };

Colour bandColour(int index)
{
    static const Colour colours[] { blue, green, yellow, orange, red, purple, teal, pink };
    return colours[((index % 8) + 8) % 8];
}

juce::Font uiFont(float size, bool bold = false, float kerning = 0.0f)
{
   #if JUCE_WINDOWS
    const juce::String name = "Segoe UI";
   #elif JUCE_MAC
    const juce::String name = "Helvetica Neue";
   #else
    const auto name = juce::Font::getDefaultSansSerifFontName();
   #endif
    return juce::Font(juce::FontOptions(name, size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(kerning);
}

constexpr double minFrequency = 20.0, maxFrequency = 20000.0;

float xForFrequency(juce::Rectangle<float> r, double frequency)
{
    const auto t = std::log(juce::jmax(1.0, frequency) / minFrequency) / std::log(maxFrequency / minFrequency);
    return r.getX() + static_cast<float>(t) * r.getWidth();
}

double frequencyForX(juce::Rectangle<float> r, float x)
{
    const auto t = juce::jlimit(0.0, 1.0, static_cast<double>((x - r.getX()) / juce::jmax(1.0f, r.getWidth())));
    return minFrequency * std::pow(maxFrequency / minFrequency, t);
}

float yForDb(juce::Rectangle<float> r, float db, float top, float bottom)
{
    return juce::jmap(juce::jlimit(bottom, top, db), top, bottom, r.getY(), r.getBottom());
}

float dbForY(juce::Rectangle<float> r, float y, float top, float bottom)
{
    return juce::jmap(juce::jlimit(r.getY(), r.getBottom(), y), r.getY(), r.getBottom(), top, bottom);
}

juce::String slotId(const juce::String& family, int slot, const juce::String& field)
{
    return family + "." + juce::String(slot).paddedLeft('0', 2) + "." + field;
}

juce::String hertz(double f)
{
    return f >= 1000.0 ? juce::String(f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz";
}

// ------------------------------------------------------------------ display math
/** Filter magnitude for drawing. The same RBJ forms the engine uses. */
struct Response
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    [[nodiscard]] double magnitude(double rate, double f) const
    {
        const auto w = juce::MathConstants<double>::twoPi * f / rate;
        const std::complex<double> e1 = std::polar(1.0, -w), e2 = std::polar(1.0, -2.0 * w);
        return std::abs((b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2));
    }

    static Response make(double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    struct Terms { double w, cs, sn, alpha, A; };
    static Terms terms(double rate, double f, double q, double db)
    {
        const auto w = juce::MathConstants<double>::twoPi * juce::jlimit(10.0, rate * 0.49, f) / rate;
        return { w, std::cos(w), std::sin(w), std::sin(w) / (2.0 * juce::jmax(0.05, q)), std::pow(10.0, db / 40.0) };
    }

    static Response peak(double rate, double f, double q, double db)
    {
        const auto t = terms(rate, f, q, db);
        return make(1 + t.alpha * t.A, -2 * t.cs, 1 - t.alpha * t.A, 1 + t.alpha / t.A, -2 * t.cs, 1 - t.alpha / t.A);
    }
    static Response lowShelf(double rate, double f, double db)
    {
        const auto t = terms(rate, f, 0.7071, db);
        const auto A = t.A, s = 2 * std::sqrt(A) * t.alpha;
        return make(A * ((A + 1) - (A - 1) * t.cs + s), 2 * A * ((A - 1) - (A + 1) * t.cs), A * ((A + 1) - (A - 1) * t.cs - s),
                    (A + 1) + (A - 1) * t.cs + s, -2 * ((A - 1) + (A + 1) * t.cs), (A + 1) + (A - 1) * t.cs - s);
    }
    static Response highShelf(double rate, double f, double db)
    {
        const auto t = terms(rate, f, 0.7071, db);
        const auto A = t.A, s = 2 * std::sqrt(A) * t.alpha;
        return make(A * ((A + 1) + (A - 1) * t.cs + s), -2 * A * ((A - 1) + (A + 1) * t.cs), A * ((A + 1) + (A - 1) * t.cs - s),
                    (A + 1) - (A - 1) * t.cs + s, 2 * ((A - 1) - (A + 1) * t.cs), (A + 1) - (A - 1) * t.cs - s);
    }
    static Response highPass(double rate, double f, double q)
    {
        const auto t = terms(rate, f, q, 0);
        return make((1 + t.cs) * 0.5, -(1 + t.cs), (1 + t.cs) * 0.5, 1 + t.alpha, -2 * t.cs, 1 - t.alpha);
    }
    static Response lowPass(double rate, double f, double q)
    {
        const auto t = terms(rate, f, q, 0);
        return make((1 - t.cs) * 0.5, 1 - t.cs, (1 - t.cs) * 0.5, 1 + t.alpha, -2 * t.cs, 1 - t.alpha);
    }
    static Response notch(double rate, double f, double q)
    {
        const auto t = terms(rate, f, q, 0);
        return make(1, -2 * t.cs, 1, 1 + t.alpha, -2 * t.cs, 1 - t.alpha);
    }
    static Response bandPass(double rate, double f, double q)
    {
        const auto t = terms(rate, f, q, 0);
        return make(t.alpha, 0, -t.alpha, 1 + t.alpha, -2 * t.cs, 1 - t.alpha);
    }
};

// ------------------------------------------------------------------ analyser
/** A smoothed FFT magnitude spectrum in dB, fed from the tap. */
class Analyser
{
public:
    static constexpr int order = 12, size = 1 << order;

    Analyser() : fft(order), window(static_cast<size_t>(size), juce::dsp::WindowingFunction<float>::hann, false)
    {
        ring.assign(static_cast<size_t>(size), 0.0f);
        data.assign(static_cast<size_t>(size) * 2, 0.0f);
        level.assign(static_cast<size_t>(size / 2), -100.0f);
    }

    void push(const float* left, const float* right, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            ring[static_cast<size_t>(position)] = 0.5f * (left[i] + right[i]);
            position = (position + 1) % size;
            if (++fresh >= size / 4) { fresh = 0; compute(); }
        }
    }

    /** Lets the picture fall when audio stops. */
    void idle()
    {
        for (auto& v : level) v = juce::jmax(-100.0f, v - 1.2f);
    }

    [[nodiscard]] float at(double frequency, double rate) const
    {
        const auto bin = frequency * size / rate;
        const auto b = juce::jlimit(1, size / 2 - 2, static_cast<int>(bin));
        const auto frac = static_cast<float>(juce::jlimit(0.0, 1.0, bin - b));
        return level[static_cast<size_t>(b)] + frac * (level[static_cast<size_t>(b + 1)] - level[static_cast<size_t>(b)]);
    }

private:
    void compute()
    {
        for (int i = 0; i < size; ++i) data[static_cast<size_t>(i)] = ring[static_cast<size_t>((position + i) % size)];
        std::fill(data.begin() + size, data.end(), 0.0f);
        window.multiplyWithWindowingTable(data.data(), static_cast<size_t>(size));
        fft.performFrequencyOnlyForwardTransform(data.data());
        for (int b = 0; b < size / 2; ++b)
        {
            const auto db = juce::Decibels::gainToDecibels(data[static_cast<size_t>(b)] * 4.0f / size, -100.0f);
            auto& v = level[static_cast<size_t>(b)];
            v += (db > v ? 0.7f : 0.22f) * (db - v);
        }
    }

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> ring, data, level;
    int position = 0, fresh = 0;
};

// ------------------------------------------------------------------ base
class Base : public DigitalDisplay
{
public:
    explicit Base(PluginProcessor& owner) : processor(owner)
    {
        left.assign(4096, 0.0f);
        right.assign(4096, 0.0f);
        setWantsKeyboardFocus(false);
    }

    void tick() override
    {
        bool any = false;
        for (int n; (n = processor.analysis.pullPre(left.data(), right.data(), 4096)) > 0;) { consumePre(n); any = true; }
        for (int n; (n = processor.analysis.pullPost(left.data(), right.data(), 4096)) > 0;) { consumePost(n); any = true; }
        if (! any) idle();
        clock += 1.0f / 30.0f;
        animate();
        repaint();
    }

protected:
    virtual void consumePre(int) {}
    virtual void consumePost(int) {}
    virtual void idle() {}
    virtual void animate() {}

    [[nodiscard]] double rate() const { return juce::jmax(8000.0, processor.analysis.sampleRate.load()); }

    [[nodiscard]] float raw(const juce::String& id, float fallback = 0.0f) const
    {
        if (const auto* v = processor.state.getRawParameterValue(id)) return v->load();
        return fallback;
    }

    [[nodiscard]] bool exists(const juce::String& id) const { return processor.state.getParameter(id) != nullptr; }

    void set(const juce::String& id, float value)
    {
        if (auto* p = processor.state.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    }

    void begin(const juce::String& id) { if (auto* p = processor.state.getParameter(id)) { p->beginChangeGesture(); held.add(id); } }
    void endAll()
    {
        for (const auto& id : held)
            if (auto* p = processor.state.getParameter(id)) p->endChangeGesture();
        held.clear();
    }

    [[nodiscard]] juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedBottom(20.0f); }

    void paintFrame(juce::Graphics& g) const
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(ink);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(juce::Colour(0xff1f242d));
        g.drawRoundedRectangle(r.reduced(0.6f), 10.0f, 1.2f);
    }

    /** Log frequency grid with labels under the plot. */
    void paintFrequencyGrid(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        for (const auto f : { 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                              2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0 })
        {
            g.setColour(gridLine);
            g.drawVerticalLine(juce::roundToInt(xForFrequency(r, f)), r.getY(), r.getBottom());
        }
        g.setFont(uiFont(12.5f));
        for (const auto& [f, label] : std::initializer_list<std::pair<double, const char*>> {
                 { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" },
                 { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
        {
            const auto x = xForFrequency(r, f);
            g.setColour(gridStrong);
            g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
            g.setColour(textDim);
            g.drawText(label, juce::Rectangle<float>(x - 20.0f, r.getBottom() + 3.0f, 40.0f, 16.0f), juce::Justification::centred, false);
        }
    }

    /** Horizontal dB lines with labels on the right edge. */
    void paintDbGrid(juce::Graphics& g, juce::Rectangle<float> r, float top, float bottom, float step, bool signedLabels) const
    {
        g.setFont(uiFont(11.5f));
        for (auto db = std::ceil(bottom / step) * step; db <= top + 0.01f; db += step)
        {
            const auto y = yForDb(r, db, top, bottom);
            g.setColour(std::abs(db) < 0.01f ? juce::Colour(0xff39414d) : gridLine);
            g.drawHorizontalLine(juce::roundToInt(y), r.getX(), r.getRight());
            g.setColour(textDim);
            const auto label = (signedLabels && db > 0 ? "+" : "") + juce::String(juce::roundToInt(db));
            g.drawText(label, juce::Rectangle<float>(r.getRight() - 34.0f, y - 8.0f, 30.0f, 16.0f), juce::Justification::centredRight, false);
        }
    }

    void paintTitle(juce::Graphics& g, const juce::String& text) const
    {
        const auto font = uiFont(12.0f, true, 0.25f);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        const auto width = juce::jmin(760.0f, glyphs.getBoundingBox(0, -1, true).getWidth());
        g.setColour(ink.withAlpha(0.85f));
        g.fillRoundedRectangle(juce::Rectangle<float>(14.0f, 11.0f, width + 14.0f, 22.0f), 5.0f);
        g.setColour(textDim);
        g.setFont(font);
        g.drawText(text, juce::Rectangle<float>(20.0f, 14.0f, 760.0f, 16.0f), juce::Justification::centredLeft, false);
    }

    PluginProcessor& processor;
    std::vector<float> left, right;
    juce::StringArray held;
    float clock = 0.0f;
};

/** A display that shows the spectrum before and after the engine. */
class SpectrumDisplay : public Base
{
public:
    using Base::Base;

protected:
    void consumePre(int n) override { before.push(left.data(), right.data(), n); }
    void consumePost(int n) override { after.push(left.data(), right.data(), n); }
    void idle() override { before.idle(); after.idle(); }

    static constexpr float analyserTop = 6.0f, analyserBottom = -90.0f;

    /** Input as a faint line, output as a soft fill, on the analyser scale. */
    void paintSpectra(juce::Graphics& g, juce::Rectangle<float> r, Colour tint) const
    {
        juce::Path pre, post;
        const auto sr = rate();
        for (auto x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            const auto f = frequencyForX(r, x);
            const auto yPre = yForDb(r, before.at(f, sr), analyserTop, analyserBottom);
            const auto yPost = yForDb(r, after.at(f, sr), analyserTop, analyserBottom);
            if (x == r.getX()) { pre.startNewSubPath(x, yPre); post.startNewSubPath(x, yPost); }
            else { pre.lineTo(x, yPre); post.lineTo(x, yPost); }
        }
        auto fill = post;
        fill.lineTo(r.getRight(), r.getBottom());
        fill.lineTo(r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(tint.withAlpha(0.28f), 0.0f, r.getY(), tint.withAlpha(0.03f), 0.0f, r.getBottom(), false));
        g.fillPath(fill);
        g.setColour(tint.withAlpha(0.55f));
        g.strokePath(post, juce::PathStrokeType(1.2f));
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.strokePath(pre, juce::PathStrokeType(1.0f));
    }

    /** What the engine is actually doing: output minus input, measured. */
    [[nodiscard]] float measuredChange(double frequency) const
    {
        const auto sr = rate();
        const auto in = before.at(frequency, sr);
        if (in < -75.0f) return 0.0f;
        return juce::jlimit(-24.0f, 24.0f, after.at(frequency, sr) - in);
    }

    void paintMeasuredChange(juce::Graphics& g, juce::Rectangle<float> r, float top, float bottom, Colour colour,
                             double from = minFrequency, double to = maxFrequency) const
    {
        juce::Path curve;
        const auto zero = yForDb(r, 0.0f, top, bottom);
        const auto x0 = xForFrequency(r, from), x1 = xForFrequency(r, to);
        bool started = false;
        for (auto x = x0; x <= x1; x += 2.0f)
        {
            const auto y = yForDb(r, measuredChange(frequencyForX(r, x)), top, bottom);
            if (! started) { curve.startNewSubPath(x, zero); started = true; }
            curve.lineTo(x, y);
        }
        if (! started) return;
        auto fill = curve;
        fill.lineTo(x1, zero);
        fill.closeSubPath();
        g.setColour(colour.withAlpha(0.22f));
        g.fillPath(fill);
        g.setColour(colour);
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    Analyser before, after;
};

// ================================================================== EQ
/** PRISM and FLUX: the curve, the spectrum, grabbable nodes and per-band
    stereo placement, the way a modern digital equaliser works. */
class EqDisplay final : public SpectrumDisplay
{
public:
    EqDisplay(PluginProcessor& owner, bool dynamicOnly)
        : SpectrumDisplay(owner), flux(dynamicOnly), slots(dynamicOnly ? 12 : 24) {}

    void setSelectedSlot(const juce::String& family, int slot) override
    {
        if (family == "band") selected = slot;
    }

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        paintFrequencyGrid(g, r);
        paintDbGrid(g, r, dbRange(), -dbRange(), flux ? 6.0f : 6.0f, true);
        paintSpectra(g, r, blue);

        // Each active band as its own soft shape, the selected one strongest.
        for (int s = 1; s <= slots; ++s)
        {
            if (! active(s)) continue;
            juce::Path shape;
            const auto zero = yForDb(r, 0.0f, dbRange(), -dbRange());
            shape.startNewSubPath(r.getX(), zero);
            for (auto x = r.getX(); x <= r.getRight(); x += 3.0f)
            {
                const auto f = frequencyForX(r, x);
                shape.lineTo(x, yForDb(r, toDb(response(s, true).magnitude(displayRate, f)), dbRange(), -dbRange()));
            }
            shape.lineTo(r.getRight(), zero);
            shape.closeSubPath();
            g.setColour(bandColour(s - 1).withAlpha(s == selected ? 0.30f : 0.12f));
            g.fillPath(shape);
        }

        // The combined curve: what is set, and what is happening right now.
        juce::Path setCurve, liveCurve;
        bool anyLive = false;
        for (auto x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            const auto f = frequencyForX(r, x);
            double set = 1.0, live = 1.0;
            for (int s = 1; s <= slots; ++s)
            {
                if (! active(s)) continue;
                set *= response(s, false).magnitude(displayRate, f);
                live *= response(s, true).magnitude(displayRate, f);
            }
            const auto ySet = yForDb(r, toDb(set), dbRange(), -dbRange());
            const auto yLive = yForDb(r, toDb(live), dbRange(), -dbRange());
            anyLive = anyLive || std::abs(ySet - yLive) > 0.5f;
            if (x == r.getX()) { setCurve.startNewSubPath(x, ySet); liveCurve.startNewSubPath(x, yLive); }
            else { setCurve.lineTo(x, ySet); liveCurve.lineTo(x, yLive); }
        }
        g.setColour(juce::Colours::white.withAlpha(flux ? 0.35f : 0.9f));
        g.strokePath(setCurve, juce::PathStrokeType(flux ? 1.4f : 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (anyLive || flux)
        {
            g.setColour(yellow);
            g.strokePath(liveCurve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        paintNodes(g, r);
        paintPlacement(g);
        paintReadout(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (placementHit(e.position)) return;

        const auto hit = nodeAt(e.position);
        if (hit == 0) return;
        select(hit);

        if (e.mods.isPopupMenu()) { showBandMenu(hit); return; }

        begin(slotId("band", hit, "frequency"));
        begin(slotId("band", hit, verticalField()));
        dragging = hit;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        const auto r = plot();
        set(slotId("band", dragging, "frequency"), static_cast<float>(frequencyForX(r, e.position.x)));
        if (! isCut(dragging))
        {
            auto db = dbForY(r, e.position.y, dbRange(), -dbRange());
            if (flux) db = juce::jlimit(-24.0f, 24.0f, db);
            set(slotId("band", dragging, verticalField()), db);
        }
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); dragging = 0; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto hit = nodeAt(e.position) != 0 ? nodeAt(e.position) : selected;
        if (! active(hit)) return;
        const auto id = slotId("band", hit, "q");
        const auto q = raw(id, 1.0f) * std::pow(1.15f, wheel.deltaY * 4.0f);
        begin(id);
        set(id, q);
        endAll();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto hit = nodeAt(e.position);
        const auto r = plot();
        if (hit != 0)
        {
            // Double-click a node: PRISM removes the band, FLUX resets its range.
            if (flux) { begin(slotId("band", hit, "range")); set(slotId("band", hit, "range"), 0.0f); }
            else { begin(slotId("band", hit, "enabled")); set(slotId("band", hit, "enabled"), 0.0f); }
            endAll();
            return;
        }

        // Double-click empty space: a new band where you clicked.
        const auto frequency = static_cast<float>(frequencyForX(r, e.position.x));
        const auto db = dbForY(r, e.position.y, dbRange(), -dbRange());
        int slot = 0;
        if (flux)
        {
            const auto count = juce::roundToInt(raw("band_count", 4.0f));
            if (count >= slots) return;
            slot = count + 1;
            begin("band_count");
            set("band_count", static_cast<float>(slot));
        }
        else
        {
            for (int s = 1; s <= slots && slot == 0; ++s)
                if (! active(s)) slot = s;
            if (slot == 0) return;
            begin(slotId("band", slot, "enabled"));
            set(slotId("band", slot, "enabled"), 1.0f);
            begin(slotId("band", slot, "type"));
            set(slotId("band", slot, "type"), 0.0f);
        }
        for (const auto* field : { "frequency", verticalField() }) begin(slotId("band", slot, field));
        set(slotId("band", slot, "frequency"), frequency);
        set(slotId("band", slot, verticalField()), db);
        endAll();
        select(slot);
    }

private:
    static constexpr double displayRate = 96000.0;

    [[nodiscard]] float dbRange() const { return flux ? 24.0f : 18.0f; }
    [[nodiscard]] const char* verticalField() const { return flux ? "range" : "gain"; }
    static float toDb(double magnitude) { return static_cast<float>(juce::Decibels::gainToDecibels(magnitude, -60.0)); }

    [[nodiscard]] bool active(int s) const
    {
        if (s < 1 || s > slots) return false;
        if (flux) return s <= juce::roundToInt(raw("band_count", 4.0f));
        return raw(slotId("band", s, "enabled"), s <= 6 ? 1.0f : 0.0f) > 0.5f;
    }

    [[nodiscard]] int type(int s) const { return flux ? 0 : juce::roundToInt(raw(slotId("band", s, "type"))); }
    [[nodiscard]] bool isCut(int s) const { const auto t = type(s); return t >= 3 && t <= 6; }

    /** One band's shape. `live` adds what the dynamics are doing right now. */
    [[nodiscard]] Response response(int s, bool live) const
    {
        const auto f = static_cast<double>(raw(slotId("band", s, "frequency"), 1000.0f));
        const auto q = static_cast<double>(raw(slotId("band", s, "q"), 1.0f));
        const auto activity = live ? static_cast<double>(processor.getBandActivityDb(s - 1)) : 0.0;
        if (flux)
        {
            // FLUX sits flat until the music pushes it; "set" shows its reach.
            const auto reach = live ? activity : static_cast<double>(raw(slotId("band", s, "range"), -4.0f));
            return Response::peak(displayRate, f, q, reach);
        }
        const auto gain = static_cast<double>(raw(slotId("band", s, "gain"))) + activity;
        switch (type(s))
        {
            case 1: return Response::lowShelf(displayRate, f, gain);
            case 2: return Response::highShelf(displayRate, f, gain);
            case 3: return Response::highPass(displayRate, f, q);
            case 4: return Response::lowPass(displayRate, f, q);
            case 5: return Response::notch(displayRate, f, q);
            case 6: return Response::bandPass(displayRate, f, q);
            case 7: return Response::peak(displayRate, f, q, gain * 0.5);
            default: return Response::peak(displayRate, f, q, gain);
        }
    }

    [[nodiscard]] juce::Point<float> nodePosition(int s) const
    {
        const auto r = plot();
        const auto f = raw(slotId("band", s, "frequency"), 1000.0f);
        const auto db = isCut(s) ? 0.0f : raw(slotId("band", s, verticalField()));
        return { xForFrequency(r, f), yForDb(r, db, dbRange(), -dbRange()) };
    }

    [[nodiscard]] int nodeAt(juce::Point<float> p) const
    {
        int best = 0;
        auto bestDistance = 16.0f;
        for (int s = 1; s <= slots; ++s)
        {
            if (! active(s)) continue;
            const auto d = nodePosition(s).getDistanceFrom(p);
            if (d < bestDistance) { bestDistance = d; best = s; }
        }
        return best;
    }

    void select(int s)
    {
        selected = s;
        if (onSelectSlot) onSelectSlot("band", s);
        repaint();
    }

    void paintNodes(juce::Graphics& g, juce::Rectangle<float>) const
    {
        static const char* placement[] { "", "L", "R", "M", "S" };
        for (int s = 1; s <= slots; ++s)
        {
            if (! active(s)) continue;
            const auto p = nodePosition(s);
            const auto colour = bandColour(s - 1);
            const auto radius = s == selected ? 12.0f : 10.0f;
            if (s == selected)
            {
                g.setColour(colour.withAlpha(0.25f));
                g.fillEllipse(juce::Rectangle<float>(radius * 3.2f, radius * 3.2f).withCentre(p));
            }
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(p));
            g.setColour(colour);
            g.fillEllipse(juce::Rectangle<float>(radius * 2.0f - 4.0f, radius * 2.0f - 4.0f).withCentre(p));
            g.setColour(juce::Colours::black.withAlpha(0.8f));
            g.setFont(uiFont(11.0f, true));
            g.drawText(juce::String(s), juce::Rectangle<float>(24.0f, 16.0f).withCentre(p), juce::Justification::centred, false);

            const auto mode = juce::jlimit(0, 4, juce::roundToInt(raw(slotId("band", s, "stereo_mode"))));
            if (mode != 0)
            {
                const auto badge = juce::Rectangle<float>(16.0f, 16.0f).withCentre(p.translated(radius + 6.0f, -radius - 4.0f));
                g.setColour(colour);
                g.fillRoundedRectangle(badge, 4.0f);
                g.setColour(juce::Colours::black);
                g.setFont(uiFont(11.0f, true));
                g.drawText(placement[mode], badge, juce::Justification::centred, false);
            }
        }
    }

    // ---- stereo placement strip for the selected band
    [[nodiscard]] juce::Rectangle<float> placementArea() const
    {
        return { 24.0f, 58.0f, 330.0f, 30.0f };
    }

    void paintPlacement(juce::Graphics& g) const
    {
        if (! active(selected)) return;
        static const char* names[] { "STEREO", "LEFT", "RIGHT", "MID", "SIDE" };
        const auto area = placementArea();
        const auto mode = juce::jlimit(0, 4, juce::roundToInt(raw(slotId("band", selected, "stereo_mode"))));
        const auto colour = bandColour(selected - 1);

        g.setColour(juce::Colour(0xe6121720));
        g.fillRoundedRectangle(area.expanded(4.0f, 22.0f).withTrimmedBottom(-4.0f).translated(0.0f, -9.0f), 8.0f);
        g.setColour(textDim);
        g.setFont(uiFont(11.0f, true, 0.2f));
        g.drawText("BAND " + juce::String(selected) + " PLACEMENT", area.translated(0.0f, -22.0f).withHeight(16.0f),
                   juce::Justification::centredLeft, false);

        const auto width = area.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            const auto cell = juce::Rectangle<float>(area.getX() + width * i, area.getY(), width, area.getHeight()).reduced(2.0f);
            g.setColour(i == mode ? colour : juce::Colour(0xff1b212b));
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(i == mode ? juce::Colours::black : textMain);
            g.setFont(uiFont(12.0f, true));
            g.drawText(names[i], cell, juce::Justification::centred, false);
        }
    }

    bool placementHit(juce::Point<float> p)
    {
        const auto area = placementArea();
        if (! active(selected) || ! area.contains(p)) return false;
        const auto index = juce::jlimit(0, 4, static_cast<int>((p.x - area.getX()) / (area.getWidth() / 5.0f)));
        const auto id = slotId("band", selected, "stereo_mode");
        begin(id);
        set(id, static_cast<float>(index));
        endAll();
        repaint();
        return true;
    }

    void paintReadout(juce::Graphics& g) const
    {
        paintTitle(g, flux ? "DYNAMIC EQ  /  DRAG A NODE, DOUBLE-CLICK TO ADD"
                           : "EQ  /  DRAG A NODE, WHEEL FOR Q, DOUBLE-CLICK TO ADD OR REMOVE");
        if (! active(selected)) return;
        const auto f = raw(slotId("band", selected, "frequency"), 1000.0f);
        const auto v = raw(slotId("band", selected, verticalField()));
        const auto q = raw(slotId("band", selected, "q"), 1.0f);
        const auto live = processor.getBandActivityDb(selected - 1);
        juce::String text = "BAND " + juce::String(selected) + "   " + hertz(f) + "   ";
        if (! isCut(selected)) text << (v > 0 ? "+" : "") << juce::String(v, 1) << " dB   ";
        text << "Q " << juce::String(q, 2);
        if (std::abs(live) > 0.05f) text << "   NOW " << (live > 0 ? "+" : "") << juce::String(live, 1) << " dB";
        g.setColour(bandColour(selected - 1));
        g.setFont(uiFont(14.0f, true));
        g.drawText(text, juce::Rectangle<float>(static_cast<float>(getWidth()) - 560.0f, 12.0f, 500.0f, 18.0f),
                   juce::Justification::centredRight, false);
    }

    void showBandMenu(int s)
    {
        juce::PopupMenu menu;
        if (! flux)
        {
            juce::PopupMenu types;
            static const char* names[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
            for (int i = 0; i < 8; ++i) types.addItem(100 + i, names[i], true, type(s) == i);
            menu.addSubMenu("Shape", types);
        }
        juce::PopupMenu place;
        static const char* places[] { "Stereo", "Left", "Right", "Mid", "Side" };
        const auto mode = juce::roundToInt(raw(slotId("band", s, "stereo_mode")));
        for (int i = 0; i < 5; ++i) place.addItem(200 + i, places[i], true, mode == i);
        menu.addSubMenu("Placement", place);
        if (exists(slotId("band", s, "solo")))
            menu.addItem(300, "Solo", true, raw(slotId("band", s, "solo")) > 0.5f);
        if (! flux) menu.addItem(301, "Remove band");

        juce::Component::SafePointer<EqDisplay> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safe, s](int result)
        {
            if (safe == nullptr || result <= 0) return;
            auto apply = [&](const juce::String& field, float value)
            {
                safe->begin(slotId("band", s, field));
                safe->set(slotId("band", s, field), value);
                safe->endAll();
            };
            if (result >= 100 && result < 108) apply("type", static_cast<float>(result - 100));
            else if (result >= 200 && result < 205) apply("stereo_mode", static_cast<float>(result - 200));
            else if (result == 300) apply("solo", safe->raw(slotId("band", s, "solo")) > 0.5f ? 0.0f : 1.0f);
            else if (result == 301) apply("enabled", 0.0f);
            safe->repaint();
        });
    }

    const bool flux;
    const int slots;
    int selected = 1, dragging = 0;
};

// ================================================================== MULTIBAND
class MultibandDisplay final : public SpectrumDisplay
{
public:
    using SpectrumDisplay::SpectrumDisplay;

    void setSelectedSlot(const juce::String& family, int slot) override
    {
        if (family == "band") selected = slot;
        if (family == "xover") selectedCrossover = slot;
    }

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        paintFrequencyGrid(g, r);
        paintDbGrid(g, r, 0.0f, -72.0f, 12.0f, false);

        const auto count = bandCount();
        for (int b = 1; b <= count; ++b)
        {
            const auto [lo, hi] = edges(b);
            const auto region = juce::Rectangle<float>::leftTopRightBottom(lo, r.getY(), hi, r.getBottom());
            const auto colour = bandColour(b - 1);
            const auto bypassed = raw(slotId("band", b, "bypass")) > 0.5f;
            g.setColour(colour.withAlpha(bypassed ? 0.02f : (b == selected ? 0.13f : 0.06f)));
            g.fillRect(region);

            // The band's live gain change, hanging from the top.
            const auto activity = processor.getBandActivityDb(b - 1);
            const auto depth = juce::jlimit(0.0f, 1.0f, std::abs(activity) / 24.0f) * r.getHeight() * 0.45f;
            g.setColour(colour.withAlpha(0.55f));
            if (activity < 0.0f) g.fillRect(region.withHeight(depth).reduced(6.0f, 0.0f));
            else if (activity > 0.0f) g.fillRect(region.withTop(r.getBottom() - depth).reduced(6.0f, 0.0f));

            // Threshold: grab and drag.
            const auto y = yForDb(r, raw(slotId("band", b, "threshold"), -24.0f), 0.0f, -72.0f);
            g.setColour(colour);
            g.drawLine(lo + 4.0f, y, hi - 4.0f, y, b == selected ? 2.4f : 1.4f);

            g.setColour(colour.brighter(0.3f));
            g.setFont(uiFont(13.0f, true));
            g.drawText("BAND " + juce::String(b), juce::Rectangle<float>(lo, r.getY() + 26.0f, hi - lo, 16.0f), juce::Justification::centred, false);
            g.setColour(textMain);
            g.setFont(uiFont(12.5f));
            const auto label = std::abs(activity) > 0.05f ? juce::String(activity, 1) + " dB" : juce::String("idle");
            g.drawText(bypassed ? juce::String("BYPASSED") : label,
                       juce::Rectangle<float>(lo, r.getY() + 42.0f, hi - lo, 16.0f), juce::Justification::centred, false);
        }

        paintSpectra(g, r, teal);

        for (int x = 1; x < count; ++x)
        {
            const auto px = xForFrequency(r, raw(slotId("xover", x, "frequency"), 1000.0f));
            g.setColour(juce::Colours::white.withAlpha(x == selectedCrossover ? 0.95f : 0.6f));
            g.drawLine(px, r.getY(), px, r.getBottom(), 1.6f);
            g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ px, r.getBottom() - 10.0f }));
        }

        paintTitle(g, "MULTIBAND  /  DRAG CROSSOVERS, DRAG A LINE FOR THRESHOLD");
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto r = plot();
        const auto count = bandCount();
        for (int x = 1; x < count; ++x)
            if (std::abs(xForFrequency(r, raw(slotId("xover", x, "frequency"), 1000.0f)) - e.position.x) < 7.0f)
            {
                crossover = x;
                selectedCrossover = x;
                begin(slotId("xover", x, "frequency"));
                if (onSelectSlot) onSelectSlot("xover", x);
                return;
            }

        for (int b = 1; b <= count; ++b)
        {
            const auto [lo, hi] = edges(b);
            if (e.position.x < lo || e.position.x > hi) continue;
            selected = b;
            if (onSelectSlot) onSelectSlot("band", b);
            band = b;
            begin(slotId("band", b, "threshold"));
            mouseDrag(e);
            return;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto r = plot();
        if (crossover != 0)
        {
            const auto lower = crossover > 1 ? raw(slotId("xover", crossover - 1, "frequency")) * 1.1f : 40.0f;
            const auto upper = crossover < bandCount() - 1 ? raw(slotId("xover", crossover + 1, "frequency")) / 1.1f : 18000.0f;
            set(slotId("xover", crossover, "frequency"),
                juce::jlimit(lower, upper, static_cast<float>(frequencyForX(r, e.position.x))));
        }
        else if (band != 0)
            set(slotId("band", band, "threshold"), juce::jlimit(-72.0f, 0.0f, dbForY(r, e.position.y, 0.0f, -72.0f)));
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); crossover = band = 0; }

private:
    [[nodiscard]] int bandCount() const { return juce::jlimit(2, 6, juce::roundToInt(raw("band_count", 4.0f))); }

    [[nodiscard]] std::pair<float, float> edges(int b) const
    {
        const auto r = plot();
        const auto lo = b == 1 ? r.getX() : xForFrequency(r, raw(slotId("xover", b - 1, "frequency"), 1000.0f));
        const auto hi = b == bandCount() ? r.getRight() : xForFrequency(r, raw(slotId("xover", b, "frequency"), 1000.0f));
        return { lo, juce::jmax(lo + 2.0f, hi) };
    }

    int selected = 1, selectedCrossover = 1, crossover = 0, band = 0;
};

// ================================================================== FOCUSED BANDS
/** De-esser, resonance control and shaper: a region over the spectrum, and
    the measured change the engine makes inside it. */
class FocusDisplay final : public SpectrumDisplay
{
public:
    enum class Kind { deesser, resonance, shaper };

    FocusDisplay(PluginProcessor& owner, Kind k) : SpectrumDisplay(owner), kind(k) {}

    void setSelectedSlot(const juce::String& family, int slot) override
    {
        if (family == "zone") selectedZone = slot;
    }

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        paintFrequencyGrid(g, r);
        paintDbGrid(g, r, 18.0f, -18.0f, 6.0f, true);

        if (kind == Kind::shaper) paintZones(g, r);
        else
        {
            const auto [lo, hi] = region();
            const auto area = juce::Rectangle<float>::leftTopRightBottom(xForFrequency(r, lo), r.getY(), xForFrequency(r, hi), r.getBottom());
            const auto colour = kind == Kind::deesser ? blue : purple;
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.20f), area.getCentreX(), area.getY(),
                                                   colour.withAlpha(0.04f), area.getCentreX(), area.getBottom(), false));
            g.fillRect(area);
            g.setColour(colour);
            g.drawVerticalLine(juce::roundToInt(area.getX()), r.getY(), r.getBottom());
            g.drawVerticalLine(juce::roundToInt(area.getRight()), r.getY(), r.getBottom());
            for (const auto x : { area.getX(), area.getRight() })
                g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ x, r.getBottom() - 10.0f }));
        }

        paintSpectra(g, r, kind == Kind::shaper ? orange : blue);

        if (kind == Kind::deesser)
        {
            // Threshold on the analyser scale, where the detector listens.
            const auto y = yForDb(r, raw("threshold", -24.0f), analyserTop, analyserBottom);
            const auto [lo, hi] = region();
            g.setColour(blue.brighter(0.4f));
            g.drawLine(xForFrequency(r, lo), y, xForFrequency(r, hi), y, 2.2f);
            g.setFont(uiFont(12.0f, true));
            g.drawText("THRESHOLD " + juce::String(juce::roundToInt(raw("threshold", -24.0f))) + " dB",
                       juce::Rectangle<float>(xForFrequency(r, hi) + 6.0f, y - 8.0f, 160.0f, 16.0f), juce::Justification::centredLeft, false);
        }

        const auto changeColour = kind == Kind::shaper ? yellow : kind == Kind::deesser ? juce::Colour(0xff7cc4ff) : pink;
        paintMeasuredChange(g, r, 18.0f, -18.0f, changeColour);

        paintSide(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto r = plot();
        if (kind == Kind::shaper)
        {
            for (int x = 1; x <= 5; ++x)
                if (std::abs(xForFrequency(r, raw(slotId("xover", x, "frequency"), 1000.0f)) - e.position.x) < 7.0f)
                {
                    handle = x;
                    begin(slotId("xover", x, "frequency"));
                    return;
                }
            for (int z = 1; z <= 6; ++z)
            {
                const auto [lo, hi] = zoneEdges(z);
                if (e.position.x >= lo && e.position.x <= hi)
                {
                    selectedZone = z;
                    if (onSelectSlot) onSelectSlot("zone", z);
                    zone = z;
                    begin(slotId("zone", z, "harmonics"));
                    mouseDrag(e);
                    return;
                }
            }
            return;
        }

        const auto [lo, hi] = region();
        const auto x0 = xForFrequency(r, lo), x1 = xForFrequency(r, hi);
        if (kind == Kind::resonance)
        {
            if (std::abs(e.position.x - x0) < 10.0f) { handle = 1; begin("low_freq"); }
            else if (std::abs(e.position.x - x1) < 10.0f) { handle = 2; begin("high_freq"); }
            return;
        }

        // De-esser: near the threshold line grabs it, anywhere else moves the focus.
        const auto y = yForDb(r, raw("threshold", -24.0f), analyserTop, analyserBottom);
        if (std::abs(e.position.y - y) < 10.0f && e.position.x >= x0 - 10.0f && e.position.x <= x1 + 10.0f)
        {
            handle = 3;
            begin("threshold");
        }
        else
        {
            handle = 4;
            begin("focus");
            mouseDrag(e);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto r = plot();
        const auto f = static_cast<float>(frequencyForX(r, e.position.x));
        if (kind == Kind::shaper)
        {
            if (handle != 0)
            {
                const auto lower = handle > 1 ? raw(slotId("xover", handle - 1, "frequency")) * 1.1f : 20.0f;
                const auto upper = handle < 5 ? raw(slotId("xover", handle + 1, "frequency")) / 1.1f : 20000.0f;
                set(slotId("xover", handle, "frequency"), juce::jlimit(lower, upper, f));
            }
            else if (zone != 0)
            {
                const auto t = 1.0f - (e.position.y - r.getY()) / r.getHeight();
                set(slotId("zone", zone, "harmonics"), juce::jlimit(0.0f, 48.0f, t * 48.0f));
            }
            return;
        }
        if (handle == 1) set("low_freq", juce::jmin(f, raw("high_freq", 18000.0f) / 1.2f));
        if (handle == 2) set("high_freq", juce::jmax(f, raw("low_freq", 80.0f) * 1.2f));
        if (handle == 3) set("threshold", juce::jlimit(-60.0f, 0.0f, dbForY(r, e.position.y, analyserTop, analyserBottom)));
        if (handle == 4) set("focus", juce::jlimit(1500.0f, 16000.0f, f));
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); handle = zone = 0; }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (kind != Kind::deesser) return;
        begin("bandwidth");
        set("bandwidth", juce::jlimit(0.2f, 4.0f, raw("bandwidth", 1.2f) + wheel.deltaY * 0.8f));
        endAll();
    }

private:
    [[nodiscard]] std::pair<double, double> region() const
    {
        if (kind == Kind::resonance)
            return { raw("low_freq", 80.0f), raw("high_freq", 18000.0f) };
        const auto focus = static_cast<double>(raw("focus", 6500.0f));
        const auto half = std::pow(2.0, raw("bandwidth", 1.2f) * 0.5);
        return { focus / half, focus * half };
    }

    [[nodiscard]] std::pair<float, float> zoneEdges(int z) const
    {
        const auto r = plot();
        const auto lo = z == 1 ? r.getX() : xForFrequency(r, raw(slotId("xover", z - 1, "frequency"), 1000.0f));
        const auto hi = z == 6 ? r.getRight() : xForFrequency(r, raw(slotId("xover", z, "frequency"), 1000.0f));
        return { lo, juce::jmax(lo + 2.0f, hi) };
    }

    void paintZones(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        for (int z = 1; z <= 6; ++z)
        {
            const auto [lo, hi] = zoneEdges(z);
            const auto enabled = raw(slotId("zone", z, "enabled"), 1.0f) > 0.5f;
            const auto colour = bandColour(z - 1);
            const auto amount = raw(slotId("zone", z, "harmonics")) / 48.0f;
            const auto area = juce::Rectangle<float>::leftTopRightBottom(lo, r.getY(), hi, r.getBottom());
            g.setColour(colour.withAlpha(enabled ? (z == selectedZone ? 0.12f : 0.05f) : 0.015f));
            g.fillRect(area);
            // Harmonics as a glowing bar that breathes with the output.
            const auto breathe = 0.85f + 0.15f * std::sin(clock * 3.0f + static_cast<float>(z));
            const auto height = r.getHeight() * amount * (enabled ? breathe : 1.0f);
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(enabled ? 0.45f : 0.12f), 0.0f, r.getBottom() - height,
                                                   colour.withAlpha(0.02f), 0.0f, r.getBottom(), false));
            g.fillRect(area.withTop(r.getBottom() - height).reduced(5.0f, 0.0f));
            g.setColour(colour.brighter(0.3f).withAlpha(enabled ? 1.0f : 0.4f));
            g.setFont(uiFont(12.5f, true));
            g.drawText("ZONE " + juce::String(z), juce::Rectangle<float>(lo, r.getY() + 26.0f, hi - lo, 16.0f), juce::Justification::centred, false);
            g.setColour(textMain.withAlpha(enabled ? 1.0f : 0.4f));
            g.setFont(uiFont(12.0f));
            g.drawText(enabled ? juce::String(raw(slotId("zone", z, "harmonics")), 1) + " dB" : juce::String("OFF"),
                       juce::Rectangle<float>(lo, r.getY() + 42.0f, hi - lo, 16.0f), juce::Justification::centred, false);
        }
        for (int x = 1; x <= 5; ++x)
        {
            const auto px = xForFrequency(r, raw(slotId("xover", x, "frequency"), 1000.0f));
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.drawLine(px, r.getY(), px, r.getBottom(), 1.4f);
            g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ px, r.getBottom() - 10.0f }));
        }
    }

    /** A reduction meter measured inside the region, on the right. */
    void paintSide(juce::Graphics& g) const
    {
        if (kind == Kind::shaper)
        {
            paintTitle(g, "HARMONIC ZONES  /  DRAG A ZONE UP FOR MORE, DRAG THE LINES TO SPLIT");
            return;
        }
        const auto [lo, hi] = region();
        float sum = 0.0f;
        int count = 0;
        for (auto f = lo; f <= hi; f *= 1.06)
        {
            sum += measuredChange(f);
            ++count;
        }
        const auto change = count > 0 ? sum / static_cast<float>(count) : 0.0f;
        paintTitle(g, kind == Kind::deesser ? "DE-ESSER  /  DRAG TO FOCUS, WHEEL FOR WIDTH, GRAB THE LINE FOR THRESHOLD"
                                            : "RESONANCE  /  DRAG THE EDGES TO SET THE RANGE");
        g.setColour(kind == Kind::deesser ? juce::Colour(0xff7cc4ff) : pink);
        g.setFont(uiFont(14.0f, true));
        g.drawText("REDUCING " + juce::String(juce::jmin(0.0f, change), 1) + " dB   " + hertz(lo) + " to " + hertz(hi),
                   juce::Rectangle<float>(static_cast<float>(getWidth()) - 560.0f, 12.0f, 500.0f, 18.0f),
                   juce::Justification::centredRight, false);
    }

    const Kind kind;
    int handle = 0, zone = 0, selectedZone = 1;
};

// ================================================================== LIMITER
class LimiterDisplay final : public Base
{
public:
    using Base::Base;

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        const auto top = 3.0f, bottom = -36.0f;
        paintDbGrid(g, r, top, bottom, 6.0f, false);

        const auto columns = static_cast<int>(history.size());
        if (columns > 1)
        {
            const auto step = r.getWidth() / static_cast<float>(capacity - 1);
            auto x0 = r.getRight() - step * static_cast<float>(columns - 1);
            juce::Path in, out, reduction;
            in.startNewSubPath(x0, r.getBottom());
            out.startNewSubPath(x0, r.getBottom());
            reduction.startNewSubPath(x0, r.getY());
            for (int i = 0; i < columns; ++i)
            {
                const auto x = x0 + step * static_cast<float>(i);
                const auto& c = history[static_cast<size_t>(i)];
                in.lineTo(x, yForDb(r, c.input, top, bottom));
                out.lineTo(x, yForDb(r, c.output, top, bottom));
                reduction.lineTo(x, r.getY() + juce::jlimit(0.0f, 1.0f, c.reduction / 18.0f) * r.getHeight() * 0.6f);
            }
            for (auto* p : { &in, &out }) { p->lineTo(r.getRight(), r.getBottom()); p->closeSubPath(); }
            reduction.lineTo(r.getRight(), r.getY());
            reduction.closeSubPath();

            g.setColour(juce::Colour(0xff3a4250).withAlpha(0.7f));
            g.fillPath(in);
            g.setGradientFill(juce::ColourGradient(blue.withAlpha(0.75f), 0.0f, r.getY(), blue.withAlpha(0.25f), 0.0f, r.getBottom(), false));
            g.fillPath(out);
            g.setGradientFill(juce::ColourGradient(red.withAlpha(0.8f), 0.0f, r.getY(), red.withAlpha(0.2f), 0.0f, r.getY() + r.getHeight() * 0.6f, false));
            g.fillPath(reduction);
        }

        const auto ceilingY = yForDb(r, raw("ceiling", -1.0f), top, bottom);
        g.setColour(yellow);
        g.drawLine(r.getX(), ceilingY, r.getRight(), ceilingY, 1.6f);
        g.setFont(uiFont(12.0f, true));
        g.drawText("CEILING " + juce::String(raw("ceiling", -1.0f), 1), juce::Rectangle<float>(r.getX() + 6.0f, ceilingY + 2.0f, 200.0f, 16.0f),
                   juce::Justification::centredLeft, false);

        const auto thresholdY = yForDb(r, raw("threshold", -8.0f), top, bottom);
        g.setColour(blue.brighter(0.3f));
        const float dashes[] { 6.0f, 5.0f };
        g.drawDashedLine(juce::Line<float>(r.getX(), thresholdY, r.getRight(), thresholdY), dashes, 2, 1.4f);
        g.drawText("THRESHOLD " + juce::String(raw("threshold", -8.0f), 1), juce::Rectangle<float>(r.getX() + 6.0f, thresholdY + 2.0f, 200.0f, 16.0f),
                   juce::Justification::centredLeft, false);

        paintTitle(g, "LIMITER  /  GREY IN, BLUE OUT, RED IS GAIN REDUCTION");
        const auto now = history.empty() ? 0.0f : history.back().reduction;
        g.setColour(red.brighter(0.2f));
        g.setFont(uiFont(15.0f, true));
        g.drawText("GR " + juce::String(-now, 1) + " dB", juce::Rectangle<float>(static_cast<float>(getWidth()) - 260.0f, 12.0f, 200.0f, 18.0f),
                   juce::Justification::centredRight, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto r = plot();
        const auto ceilingY = yForDb(r, raw("ceiling", -1.0f), 3.0f, -36.0f);
        target = std::abs(e.position.y - ceilingY) < 10.0f ? "ceiling" : "threshold";
        begin(target);
        mouseDrag(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto db = dbForY(plot(), e.position.y, 3.0f, -36.0f);
        set(target, target == "ceiling" ? juce::jlimit(-3.0f, 0.0f, db) : juce::jlimit(-36.0f, 0.0f, db));
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); }

private:
    struct Column { float input, output, reduction; };
    static constexpr int capacity = 340;

    void consumePre(int n) override { accumulate(n, pendingIn); }
    void consumePost(int n) override { accumulate(n, pendingOut); }

    void accumulate(int n, float& peak)
    {
        for (int i = 0; i < n; ++i) peak = juce::jmax(peak, std::abs(left[static_cast<size_t>(i)]), std::abs(right[static_cast<size_t>(i)]));
    }

    void animate() override
    {
        const auto in = juce::Decibels::gainToDecibels(pendingIn, -60.0f);
        const auto out = juce::Decibels::gainToDecibels(pendingOut, -60.0f);
        // The engine drives the input up by -threshold and holds the peak at
        // the ceiling; anything beyond that is gain reduction.
        const auto driven = in - raw("threshold", -8.0f) + raw("ceiling", -1.0f);
        const auto reduction = pendingIn > 1.0e-4f ? juce::jmax(0.0f, driven - out) : 0.0f;
        history.push_back({ in, out, reduction });
        while (static_cast<int>(history.size()) > capacity) history.pop_front();
        pendingIn = pendingOut = 0.0f;
    }

    std::deque<Column> history;
    float pendingIn = 0.0f, pendingOut = 0.0f;
    juce::String target = "threshold";
};

// ================================================================== DELAY
class DelayDisplay final : public Base
{
public:
    using Base::Base;

    void setSelectedSlot(const juce::String& family, int slot) override
    {
        if (family == "tap") selected = slot;
    }

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        const auto span = timeSpan();

        for (auto t = 0.0f; t <= span + 0.001f; t += gridStep(span))
        {
            const auto x = xForTime(r, t);
            g.setColour(gridStrong);
            g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
            g.setColour(textDim);
            g.setFont(uiFont(12.0f));
            g.drawText(t < 1.0f ? juce::String(juce::roundToInt(t * 1000.0f)) + " ms" : juce::String(t, 2) + " s",
                       juce::Rectangle<float>(x - 30.0f, r.getBottom() + 3.0f, 60.0f, 16.0f), juce::Justification::centred, false);
        }
        const auto centre = r.getCentreY();
        g.setColour(gridStrong);
        g.drawHorizontalLine(juce::roundToInt(centre), r.getX(), r.getRight());

        // Main echo and its feedback repeats.
        const auto echo = raw("time", 375.0f) * 0.001f;
        const auto feedback = raw("feedback", 35.0f) * 0.01f;
        const auto frozen = raw("freeze") > 0.5f;
        auto amplitude = 1.0f;
        for (int k = 1; k <= 24 && amplitude > 0.02f; ++k)
        {
            const auto x = xForTime(r, echo * static_cast<float>(k)) + wobble(k);
            if (x > r.getRight()) break;
            const auto h = r.getHeight() * 0.42f * amplitude;
            g.setColour(orange.withAlpha(0.25f + 0.55f * amplitude));
            g.fillRoundedRectangle(juce::Rectangle<float>(x - 2.0f, centre - h, 4.0f, h * 2.0f), 2.0f);
            amplitude *= frozen ? 1.0f : feedback;
        }

        // Taps, placed in time, height by level, lean by pan.
        for (int s = 1; s <= 8; ++s)
        {
            if (raw(slotId("tap", s, "enabled"), s == 1 ? 1.0f : 0.0f) < 0.5f) continue;
            const auto p = tapPosition(s);
            const auto pan = raw(slotId("tap", s, "pan")) * 0.01f;
            const auto colour = blue.interpolatedWith(pink, (pan + 1.0f) * 0.5f);
            g.setColour(colour.withAlpha(0.6f));
            g.drawLine(p.x, centre, p.x, p.y, 2.0f);
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(s == selected ? 22.0f : 18.0f, s == selected ? 22.0f : 18.0f).withCentre(p));
            g.setColour(colour);
            g.fillEllipse(juce::Rectangle<float>(s == selected ? 17.0f : 14.0f, s == selected ? 17.0f : 14.0f).withCentre(p));
            g.setColour(juce::Colours::black);
            g.setFont(uiFont(11.0f, true));
            g.drawText(juce::String(s), juce::Rectangle<float>(20.0f, 16.0f).withCentre(p), juce::Justification::centred, false);
        }

        // Sound travelling through the line: pulses born from the output level.
        for (const auto& pulse : pulses)
        {
            const auto x = xForTime(r, pulse.age);
            if (x > r.getRight()) continue;
            const auto fade = juce::jlimit(0.0f, 1.0f, pulse.energy * (1.0f - pulse.age / (span * 1.05f)));
            g.setColour(yellow.withAlpha(fade));
            g.fillEllipse(juce::Rectangle<float>(6.0f + 10.0f * fade, 6.0f + 10.0f * fade).withCentre({ x, centre + pulse.offset }));
        }

        if (frozen)
        {
            g.setColour(teal.withAlpha(0.12f + 0.05f * std::sin(clock * 2.0f)));
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(teal);
            g.setFont(uiFont(16.0f, true, 0.3f));
            g.drawText("FROZEN", r.withHeight(30.0f).translated(0.0f, 8.0f), juce::Justification::centred, false);
        }

        paintTitle(g, "ORBIT  /  DRAG A TAP: SIDEWAYS FOR TIME, UP AND DOWN FOR LEVEL");
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int s = 1; s <= 8; ++s)
        {
            if (raw(slotId("tap", s, "enabled"), s == 1 ? 1.0f : 0.0f) < 0.5f) continue;
            if (tapPosition(s).getDistanceFrom(e.position) < 14.0f)
            {
                selected = dragging = s;
                if (onSelectSlot) onSelectSlot("tap", s);
                begin(slotId("tap", s, "time"));
                begin(slotId("tap", s, "level"));
                return;
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        const auto r = plot();
        const auto t = (e.position.x - r.getX()) / r.getWidth() * timeSpan();
        set(slotId("tap", dragging, "time"), juce::jlimit(0.0f, 6000.0f, t * 1000.0f));
        const auto level = juce::jmap(juce::jlimit(r.getY(), r.getCentreY(), e.position.y), r.getCentreY(), r.getY(), -60.0f, 6.0f);
        set(slotId("tap", dragging, "level"), level);
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); dragging = 0; }

private:
    struct Pulse { float age, energy, offset; };

    void consumePost(int n) override
    {
        for (int i = 0; i < n; ++i)
            energy = juce::jmax(energy, std::abs(left[static_cast<size_t>(i)]) + std::abs(right[static_cast<size_t>(i)]));
    }

    void animate() override
    {
        const auto dt = 1.0f / 30.0f;
        for (auto& p : pulses) p.age += dt;
        const auto span = timeSpan();
        pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [span](const Pulse& p) { return p.age > span * 1.05f; }), pulses.end());
        if (energy > 0.01f && pulses.size() < 400)
            pulses.push_back({ 0.0f, juce::jlimit(0.0f, 1.0f, energy * 1.6f), (random.nextFloat() - 0.5f) * 30.0f });
        energy = 0.0f;
    }

    [[nodiscard]] float timeSpan() const
    {
        auto longest = raw("time", 375.0f) * 0.001f * 3.0f;
        for (int s = 1; s <= 8; ++s)
            if (raw(slotId("tap", s, "enabled"), s == 1 ? 1.0f : 0.0f) > 0.5f)
                longest = juce::jmax(longest, raw(slotId("tap", s, "time")) * 0.001f * 1.25f);
        return juce::jlimit(0.25f, 6.0f, longest);
    }

    static float gridStep(float span) { return span <= 0.6f ? 0.1f : span <= 1.5f ? 0.25f : span <= 3.0f ? 0.5f : 1.0f; }

    [[nodiscard]] float xForTime(juce::Rectangle<float> r, float seconds) const
    {
        return r.getX() + seconds / timeSpan() * r.getWidth();
    }

    /** Modulation makes the repeats sway, so the delay looks as alive as it sounds. */
    [[nodiscard]] float wobble(int k) const
    {
        const auto depth = raw("mod_depth", 3.0f);
        return std::sin(clock * juce::MathConstants<float>::twoPi * raw("mod_rate", 0.35f) + static_cast<float>(k)) * depth * 0.6f;
    }

    [[nodiscard]] juce::Point<float> tapPosition(int s) const
    {
        const auto r = plot();
        const auto level = raw(slotId("tap", s, "level"), -6.0f);
        const auto y = juce::jmap(juce::jlimit(-60.0f, 6.0f, level), -60.0f, 6.0f, r.getCentreY(), r.getY() + 12.0f);
        return { xForTime(r, raw(slotId("tap", s, "time"), 250.0f) * 0.001f) + wobble(s), y };
    }

    std::vector<Pulse> pulses;
    juce::Random random;
    float energy = 0.0f;
    int selected = 1, dragging = 0;
};

// ================================================================== REVERB
class ReverbDisplay final : public Base
{
public:
    using Base::Base;

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto r = plot();
        const auto pre = raw("pre_delay", 25.0f) * 0.001f;
        const auto decay = raw("decay", 2.4f);
        const auto span = juce::jlimit(0.5f, 12.0f, pre + decay * 1.4f);
        const auto x = [&](float t) { return r.getX() + t / span * r.getWidth(); };

        for (auto t = 0.0f; t <= span; t += span > 4.0f ? 1.0f : 0.5f)
        {
            g.setColour(gridStrong);
            g.drawVerticalLine(juce::roundToInt(x(t)), r.getY(), r.getBottom());
            g.setColour(textDim);
            g.setFont(uiFont(12.0f));
            g.drawText(juce::String(t, 1) + " s", juce::Rectangle<float>(x(t) - 24.0f, r.getBottom() + 3.0f, 48.0f, 16.0f), juce::Justification::centred, false);
        }

        // The tail: an envelope that falls 60 dB over the decay time, breathing
        // with the modulation, lifted by what is playing now.
        const auto frozen = raw("freeze") > 0.5f;
        const auto lift = 0.55f + 0.45f * level;
        const auto width = raw("width", 120.0f) / 200.0f;
        const auto mod = raw("mod_depth", 20.0f) * 0.01f;
        const auto base = r.getBottom();
        juce::Path tail;
        tail.startNewSubPath(x(pre), base);
        for (auto t = pre; t <= span; t += span / 300.0f)
        {
            const auto since = t - pre;
            const auto env = frozen ? 1.0f : std::pow(10.0f, -3.0f * since / juce::jmax(0.1f, decay));
            const auto ripple = 1.0f + mod * 0.08f * std::sin(t * 23.0f + clock * juce::MathConstants<float>::twoPi * raw("mod_rate", 0.35f) * 4.0f);
            tail.lineTo(x(t), base - r.getHeight() * 0.85f * env * ripple * lift);
        }
        tail.lineTo(x(span), base);
        tail.closeSubPath();
        g.setGradientFill(juce::ColourGradient(purple.withAlpha(0.65f), x(pre), r.getY(), blue.withAlpha(0.08f), x(span), base, false));
        g.fillPath(tail);
        g.setColour(purple.brighter(0.3f));
        g.strokePath(tail, juce::PathStrokeType(1.6f));

        // Early reflections: spikes whose count follows density and spread follows size.
        const auto count = 4 + juce::roundToInt(raw("density", 65.0f) * 0.2f);
        const auto spread = 0.02f + raw("size", 70.0f) * 0.0012f;
        const auto early = juce::jlimit(0.0f, 1.0f, 0.5f + raw("early_late") * 0.005f);
        for (int i = 0; i < count; ++i)
        {
            const auto t = pre + spread * (static_cast<float>(i) + 0.3f * std::sin(static_cast<float>(i) * 7.1f)) / static_cast<float>(count) * 3.0f;
            const auto h = r.getHeight() * 0.9f * early * lift * (1.0f - static_cast<float>(i) / static_cast<float>(count + 2));
            g.setColour(juce::Colours::white.withAlpha(0.35f + 0.4f * early));
            g.drawLine(x(t), base, x(t), base - h, 1.6f);
        }

        // Particles rising out of the space.
        for (const auto& p : particles)
        {
            const auto alpha = juce::jlimit(0.0f, 1.0f, p.life) * 0.8f;
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre({ x(p.t), base - p.height * r.getHeight() }));
        }

        // Width as a halo on the right.
        g.setColour(teal.withAlpha(0.18f));
        g.fillRoundedRectangle(juce::Rectangle<float>(r.getRight() - 90.0f, r.getCentreY() - 40.0f * (0.3f + width),
                                                      80.0f, 80.0f * (0.3f + width)), 30.0f);

        g.setColour(juce::Colour(0xff39414d));
        g.drawVerticalLine(juce::roundToInt(x(pre)), r.getY(), r.getBottom());
        g.setColour(textMain);
        g.setFont(uiFont(12.0f, true));
        g.drawText("PRE " + juce::String(juce::roundToInt(pre * 1000.0f)) + " ms", juce::Rectangle<float>(x(pre) + 6.0f, r.getY() + 4.0f, 120.0f, 16.0f),
                   juce::Justification::centredLeft, false);

        paintTitle(g, frozen ? "SPACEVERB  /  FROZEN" : "SPACEVERB  /  DRAG SIDEWAYS FOR DECAY, UP AND DOWN FOR SIZE");
        g.setColour(purple.brighter(0.4f));
        g.setFont(uiFont(15.0f, true));
        g.drawText("DECAY " + juce::String(decay, 1) + " s   SIZE " + juce::String(juce::roundToInt(raw("size", 70.0f))) + " %",
                   juce::Rectangle<float>(static_cast<float>(getWidth()) - 400.0f, 12.0f, 340.0f, 18.0f), juce::Justification::centredRight, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        begin("decay");
        begin("size");
        start = e.position;
        startDecay = raw("decay", 2.4f);
        startSize = raw("size", 70.0f);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto dx = (e.position.x - start.x) / static_cast<float>(getWidth());
        const auto dy = (start.y - e.position.y) / static_cast<float>(getHeight());
        set("decay", juce::jlimit(0.1f, 40.0f, startDecay * std::pow(8.0f, dx)));
        set("size", juce::jlimit(0.0f, 100.0f, startSize + dy * 100.0f));
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); }

private:
    struct Particle { float t, height, life, rise; };

    void consumePost(int n) override
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = juce::jmax(peak, std::abs(left[static_cast<size_t>(i)]), std::abs(right[static_cast<size_t>(i)]));
        incoming = juce::jmax(incoming, peak);
    }

    void animate() override
    {
        level += (juce::jlimit(0.0f, 1.0f, incoming * 2.0f) - level) * (incoming > level ? 0.5f : 0.08f);
        incoming = 0.0f;
        const auto decay = raw("decay", 2.4f);
        const auto pre = raw("pre_delay", 25.0f) * 0.001f;
        const auto span = juce::jlimit(0.5f, 12.0f, pre + decay * 1.4f);
        for (auto& p : particles)
        {
            p.height += p.rise;
            p.life -= 1.0f / (30.0f * juce::jlimit(0.4f, 6.0f, decay));
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(),
                                       [](const Particle& p) { return p.life <= 0.0f || p.height > 1.0f; }), particles.end());
        const auto spawn = juce::roundToInt(level * 6.0f);
        for (int i = 0; i < spawn && particles.size() < 300; ++i)
        {
            const auto t = pre + random.nextFloat() * (span - pre);
            const auto env = std::pow(10.0f, -3.0f * (t - pre) / juce::jmax(0.1f, decay));
            particles.push_back({ t, random.nextFloat() * env * 0.8f, 1.0f, 0.002f + random.nextFloat() * 0.004f });
        }
    }

    std::vector<Particle> particles;
    juce::Random random;
    juce::Point<float> start;
    float startDecay = 2.4f, startSize = 70.0f, incoming = 0.0f, level = 0.0f;
};

// ================================================================== IMAGER
class ImagerDisplay final : public Base
{
public:
    explicit ImagerDisplay(PluginProcessor& owner) : Base(owner)
    {
        points.assign(static_cast<size_t>(pointCount), {});
    }

    void setSelectedSlot(const juce::String& family, int slot) override
    {
        if (family == "band") selected = slot;
    }

    void paint(juce::Graphics& g) override
    {
        paintFrame(g);
        const auto all = plot();

        // Vectorscope.
        const auto scopeSize = juce::jmin(all.getHeight(), 330.0f);
        const auto scope = juce::Rectangle<float>(all.getX(), all.getY(), scopeSize, scopeSize);
        const auto c = scope.getCentre();
        g.setColour(gridStrong);
        g.drawEllipse(scope.reduced(4.0f), 1.0f);
        g.drawLine(c.x, scope.getY(), c.x, scope.getBottom(), 1.0f);
        g.drawLine(scope.getX(), c.y, scope.getRight(), c.y, 1.0f);
        g.setColour(textDim);
        g.setFont(uiFont(11.0f, true));
        g.drawText("M", juce::Rectangle<float>(c.x - 10.0f, scope.getY() + 2.0f, 20.0f, 14.0f), juce::Justification::centred, false);
        g.drawText("L", juce::Rectangle<float>(scope.getX() + 30.0f, scope.getY() + 30.0f, 20.0f, 14.0f), juce::Justification::centred, false);
        g.drawText("R", juce::Rectangle<float>(scope.getRight() - 50.0f, scope.getY() + 30.0f, 20.0f, 14.0f), juce::Justification::centred, false);
        // Scaled to what is playing, so quiet material still fills the scope.
        const auto radius = scopeSize * 0.46f / juce::jlimit(0.08f, 1.0f, scopePeak);
        for (int i = 0; i < pointCount; ++i)
        {
            const auto& p = points[static_cast<size_t>((writeIndex + i) % pointCount)];
            const auto age = static_cast<float>(i) / static_cast<float>(pointCount);
            g.setColour(teal.withAlpha(0.08f + 0.6f * age));
            g.fillRect(juce::Rectangle<float>(1.8f, 1.8f).withCentre({ c.x + p.x * radius, c.y - p.y * radius }));
        }

        // Correlation.
        const auto bar = juce::Rectangle<float>(scope.getRight() + 24.0f, all.getY() + 16.0f, 22.0f, all.getHeight() - 30.0f);
        g.setColour(juce::Colour(0xff151a22));
        g.fillRoundedRectangle(bar, 5.0f);
        const auto mid = bar.getCentreY();
        const auto y = juce::jmap(correlation, 1.0f, -1.0f, bar.getY(), bar.getBottom());
        g.setColour(correlation >= 0.0f ? green : red);
        g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(bar.getX() + 3.0f, juce::jmin(mid, y), bar.getRight() - 3.0f, juce::jmax(mid, y)), 3.0f);
        g.setColour(textDim);
        g.setFont(uiFont(11.0f));
        g.drawText("+1", bar.withHeight(14.0f).translated(24.0f, -2.0f), juce::Justification::centredLeft, false);
        g.drawText("-1", bar.withTop(bar.getBottom() - 14.0f).translated(24.0f, 2.0f), juce::Justification::centredLeft, false);
        g.setColour(textMain);
        g.setFont(uiFont(13.0f, true));
        g.drawText(juce::String(correlation, 2), juce::Rectangle<float>(bar.getX() - 20.0f, bar.getBottom() + 2.0f, 62.0f, 16.0f),
                   juce::Justification::centred, false);

        // Width by frequency.
        const auto area = all.withTrimmedLeft(scopeSize + 90.0f);
        paintFrequencyGrid(g, area);
        const auto bassX = xForFrequency(area, raw("bass_mono", 120.0f));
        g.setColour(orange.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(area.getX(), area.getY(), bassX, area.getBottom()));
        g.setColour(orange);
        g.drawVerticalLine(juce::roundToInt(bassX), area.getY(), area.getBottom());
        g.setFont(uiFont(12.0f, true));
        g.drawText("MONO BELOW " + hertz(raw("bass_mono", 120.0f)), juce::Rectangle<float>(bassX + 6.0f, area.getBottom() - 22.0f, 200.0f, 16.0f),
                   juce::Justification::centredLeft, false);

        const auto global = raw("global_width", 100.0f) / 100.0f;
        for (int b = 1; b <= 5; ++b)
        {
            const auto [lo, hi] = bandEdges(area, b);
            const auto width = raw(slotId("band", b, "width"), 100.0f) / 100.0f * global;
            const auto half = juce::jlimit(0.02f, 1.0f, width / 2.5f) * area.getHeight() * 0.45f;
            const auto sway = 1.0f + 0.04f * std::sin(clock * 2.5f + static_cast<float>(b));
            const auto colour = bandColour(b - 1);
            const auto shape = juce::Rectangle<float>::leftTopRightBottom(lo + 4.0f, area.getCentreY() - half * sway,
                                                                          hi - 4.0f, area.getCentreY() + half * sway);
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(b == selected ? 0.55f : 0.3f), shape.getCentreX(), shape.getCentreY(),
                                                   colour.withAlpha(0.05f), shape.getCentreX(), shape.getY(), true));
            g.fillRoundedRectangle(shape, 8.0f);
            g.setColour(colour.brighter(0.3f));
            g.setFont(uiFont(12.5f, true));
            g.drawText(juce::String(juce::roundToInt(width * 100.0f)) + " %", juce::Rectangle<float>(lo, area.getY() + 6.0f, hi - lo, 16.0f),
                       juce::Justification::centred, false);
        }
        for (int x = 1; x <= 4; ++x)
        {
            const auto px = xForFrequency(area, raw(slotId("xover", x, "frequency"), 1000.0f));
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawLine(px, area.getY(), px, area.getBottom(), 1.3f);
            g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre({ px, area.getBottom() - 10.0f }));
        }

        paintTitle(g, raw("mono_check") > 0.5f ? "IMAGER  /  MONO CHECK ON" : "IMAGER  /  DRAG A BAND UP OR DOWN FOR WIDTH, DRAG THE LINES TO SPLIT");
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto area = plot().withTrimmedLeft(juce::jmin(plot().getHeight(), 330.0f) + 90.0f);
        if (! area.contains(e.position)) return;
        for (int x = 1; x <= 4; ++x)
            if (std::abs(xForFrequency(area, raw(slotId("xover", x, "frequency"), 1000.0f)) - e.position.x) < 7.0f)
            {
                crossover = x;
                begin(slotId("xover", x, "frequency"));
                return;
            }
        for (int b = 1; b <= 5; ++b)
        {
            const auto [lo, hi] = bandEdges(area, b);
            if (e.position.x < lo || e.position.x > hi) continue;
            band = selected = b;
            if (onSelectSlot) onSelectSlot("band", b);
            begin(slotId("band", b, "width"));
            mouseDrag(e);
            return;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto area = plot().withTrimmedLeft(juce::jmin(plot().getHeight(), 330.0f) + 90.0f);
        if (crossover != 0)
        {
            const auto lower = crossover > 1 ? raw(slotId("xover", crossover - 1, "frequency")) * 1.1f : 40.0f;
            const auto upper = crossover < 4 ? raw(slotId("xover", crossover + 1, "frequency")) / 1.1f : 18000.0f;
            set(slotId("xover", crossover, "frequency"), juce::jlimit(lower, upper, static_cast<float>(frequencyForX(area, e.position.x))));
        }
        else if (band != 0)
        {
            const auto distance = std::abs(e.position.y - area.getCentreY()) / (area.getHeight() * 0.45f);
            set(slotId("band", band, "width"), juce::jlimit(0.0f, 250.0f, distance * 250.0f));
        }
    }

    void mouseUp(const juce::MouseEvent&) override { endAll(); crossover = band = 0; }

private:
    static constexpr int pointCount = 2048;

    void consumePost(int n) override
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto l = left[static_cast<size_t>(i)], r = right[static_cast<size_t>(i)];
            lr += l * r; ll += l * l; rr += r * r;
            if (i % 4 == 0)
            {
                auto side = (l - r) * 0.7071f, sum = (l + r) * 0.7071f;
                const auto magnitude = std::sqrt(side * side + sum * sum);
                if (magnitude > 1.0f) { side /= magnitude; sum /= magnitude; }
                points[static_cast<size_t>(writeIndex)] = { side, sum };
                writeIndex = (writeIndex + 1) % pointCount;
                heard = juce::jmax(heard, magnitude);
            }
        }
        const auto denominator = std::sqrt(ll * rr);
        if (denominator > 1.0e-9)
            correlation += (static_cast<float>(lr / denominator) - correlation) * 0.25f;
    }

    void idle() override
    {
        for (auto& p : points) p *= 0.9f;
        correlation *= 0.95f;
    }

    void animate() override
    {
        scopePeak = heard > scopePeak ? heard : scopePeak * 0.97f + heard * 0.03f;
        heard = 0.0f;
    }

    [[nodiscard]] std::pair<float, float> bandEdges(juce::Rectangle<float> area, int b) const
    {
        const auto lo = b == 1 ? area.getX() : xForFrequency(area, raw(slotId("xover", b - 1, "frequency"), 1000.0f));
        const auto hi = b == 5 ? area.getRight() : xForFrequency(area, raw(slotId("xover", b, "frequency"), 1000.0f));
        return { lo, juce::jmax(lo + 2.0f, hi) };
    }

    std::vector<juce::Point<float>> points;
    int writeIndex = 0, selected = 1, crossover = 0, band = 0;
    float correlation = 0.0f, heard = 0.0f, scopePeak = 0.5f;
};
}

std::unique_ptr<DigitalDisplay> DigitalDisplay::create(PluginProcessor& processor)
{
    const auto& id = processor.spec.id;
    if (id == "D01") return std::make_unique<EqDisplay>(processor, false);
    if (id == "D02") return std::make_unique<EqDisplay>(processor, true);
    if (id == "D03") return std::make_unique<MultibandDisplay>(processor);
    if (id == "D04") return std::make_unique<LimiterDisplay>(processor);
    if (id == "D05") return std::make_unique<FocusDisplay>(processor, FocusDisplay::Kind::deesser);
    if (id == "D06") return std::make_unique<FocusDisplay>(processor, FocusDisplay::Kind::resonance);
    if (id == "D07") return std::make_unique<FocusDisplay>(processor, FocusDisplay::Kind::shaper);
    if (id == "D08") return std::make_unique<DelayDisplay>(processor);
    if (id == "D09") return std::make_unique<ReverbDisplay>(processor);
    return std::make_unique<ImagerDisplay>(processor);
}
}
