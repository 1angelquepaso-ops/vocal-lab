#include "PluginEditor.h"

using namespace mx;
using namespace mxui;
static constexpr int TOP = 72;

namespace mxui
{
juce::Colour bandColour (int i)
{
    static const juce::uint32 c[] = { 0xff5ec8f2, 0xffb47cf5, 0xffff7a9c, 0xffffb35c, 0xff7be38c, 0xff4fd6c2,
                                      0xfff2e15e, 0xff8aa5ff, 0xffff8f6b, 0xffd78cff, 0xff6fe0ff, 0xffb4e66b };
    return juce::Colour (c[i % 12]);
}

LnF::LnF()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1f2229));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2c3039));
    setColour (juce::ComboBox::textColourId, TEXT);
    setColour (juce::ComboBox::arrowColourId, DIM);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1f26));
    setColour (juce::PopupMenu::textColourId, TEXT);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff6b4fb0));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, TEXT);
    setColour (juce::TextButton::textColourOffId, TEXT);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId, TEXT);
    setColour (juce::ToggleButton::tickColourId, GOLD);
    setColour (juce::Slider::textBoxTextColourId, TEXT);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void LnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) / 2.0f, cx = r.getCentreX(), cy = r.getCentreY();
    const float ang = a0 + pos * (a1 - a0);
    juce::Path bg; bg.addCentredArc (cx, cy, rad - 2, rad - 2, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff2a2e37)); g.strokePath (bg, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float start = bip ? a0 + (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (a1 - a0) : a0;
    juce::Path fg; fg.addCentredArc (cx, cy, rad - 2, rad - 2, 0, juce::jmin (start, ang), juce::jmax (start, ang), true);
    auto acc = s.findColour (juce::Slider::rotarySliderFillColourId);
    g.setColour (acc); g.strokePath (fg, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff20232a)); g.fillEllipse (cx - rad + 6, cy - rad + 6, (rad - 6) * 2, (rad - 6) * 2);
    juce::Path ptr; ptr.addRoundedRectangle (-1.5f, -(rad - 7), 3.0f, rad * 0.45f, 1.5f);
    g.setColour (TEXT); g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (cx, cy));
}

void LnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    auto acc = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (on ? acc : juce::Colour (over ? 0xff2a2e38 : 0xff1f2229).brighter (down ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? acc.brighter (0.2f) : juce::Colour (0xff2c3039));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

Knob::Knob (const juce::String& name, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, GOLD);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, DIM);
    addAndMakeVisible (slider); addAndMakeVisible (label);
}
void Knob::resized() { auto r = getLocalBounds(); label.setBounds (r.removeFromTop (14)); slider.setBounds (r); }

// ================= Graph =================
Graph::Graph (MexProcessor& p) : proc (p)
{
    fftBuf.assign (1 << 14, 0.0f); win.resize (1 << 13);
    for (size_t i = 0; i < win.size(); ++i) win[i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (win.size() - 1));
}

float Graph::xForF (double f) const
{
    auto r = plot();
    return r.getX() + (float) (std::log (f / 10.0) / std::log (30000.0 / 10.0)) * r.getWidth();
}
double Graph::fForX (float x) const
{
    auto r = plot();
    return 10.0 * std::pow (3000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())));
}
float Graph::yForDb (double db) const { auto r = plot(); return r.getCentreY() - (float) (db / 24.0) * r.getHeight() * 0.46f; }
double Graph::dbForY (float y) const { auto r = plot(); return -(y - r.getCentreY()) / (r.getHeight() * 0.46f) * 24.0; }
float Graph::yForSpec (double db) const { auto r = plot(); return r.getBottom() - (float) ((db + 90.0) / 90.0) * r.getHeight(); }

void Graph::setP (int band, const char* f, float v) { proc.setParam (MexProcessor::pid (band, f), v); }
float Graph::getP (int band, const char* f) const { return proc.state.getRawParameterValue (MexProcessor::pid (band, f))->load(); }

static bool hasGain (Type t) { return t == Type::Peak || t == Type::LowShelf || t == Type::HighShelf; }

juce::Point<float> Graph::nodePos (int i, const BandParams& b) const
{
    const float x = xForF (b.spec.freq);
    if (hasGain (b.spec.type)) return { x, yForDb (b.spec.gainDb) };
    if (b.spec.type == Type::BandPass || b.spec.type == Type::BandStop || b.spec.type == Type::Disabled) return { x, yForDb (0) };
    BandSpec s = b.spec; std::vector<ASec> secs; designBand (s, secs);
    return { x, yForDb (juce::jlimit (-24.0, 24.0, 20.0 * std::log10 (std::abs (analogResponse (secs, s.freq)) + 1e-9))) };
    (void) i;
}

int Graph::hitNode (juce::Point<float> p) const
{
    int best = -1; float bd = 12.0f;
    for (int i = 0; i < NBANDS; ++i)
    {
        BandParams b; proc.readBand (i, b);
        if (! b.used) continue;
        const float d = nodePos (i, b).getDistanceFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Graph::updateAnalyzer()
{
    if (analyzerMode == 0) { specPost.clear(); specPre.clear(); return; }
    const int N = 1 << 13;
    const int pos = proc.anPos.load();
    const double sr = proc.getSr();
    auto compute = [&] (const std::vector<float>& ring, std::vector<float>& out)
    {
        for (int i = 0; i < N; ++i) fftBuf[(size_t) i] = ring[(size_t) ((pos + i) & (MexProcessor::AN - 1))] * win[(size_t) i];
        std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
        const int W = juce::jmax (2, (int) plot().getWidth());
        if ((int) out.size() != W) out.assign ((size_t) W, -120.0f);
        for (int px = 0; px < W; ++px)
        {
            const double f0 = fForX (plot().getX() + (float) px), f1 = fForX (plot().getX() + (float) px + 1);
            int k0 = (int) (f0 * N / sr), k1 = juce::jmax (k0 + 1, (int) (f1 * N / sr));
            float m = 0;
            for (int k = k0; k < k1 && k < N / 2; ++k) m = juce::jmax (m, fftBuf[(size_t) k]);
            // +4.5 dB/oct tilt like most mix analyzers (pink reference)
            const float db = juce::Decibels::gainToDecibels (m / (N * 0.25f), -120.0f) + 4.5f * (float) std::log2 (f0 / 1000.0);
            auto& o = out[(size_t) px];
            o = db > o ? db : o + (db - o) * 0.18f;
        }
    };
    compute (proc.anPost, specPost);
    if (analyzerMode == 2) compute (proc.anPre, specPre); else specPre.clear();
}

void Graph::paint (juce::Graphics& g)
{
    auto r = plot();
    g.fillAll (GRAPH);
    // grid
    g.setFont (juce::FontOptions (10.5f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xForF (f);
        g.setColour (GRID); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (DIM);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 20, (int) r.getBottom() + 1, 40, 12, juce::Justification::centred);
    }
    for (int db = -24; db <= 24; db += 6)
    {
        const float y = yForDb (db);
        g.setColour (db == 0 ? GRID.brighter (0.25f) : GRID); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (DIM); g.drawText ((db > 0 ? "+" : "") + juce::String (db), 2, (int) y - 6, 28, 12, juce::Justification::centredRight);
    }
    // analyzer
    auto specPath = [&] (const std::vector<float>& s, bool fill)
    {
        juce::Path p; const int W = (int) s.size();
        for (int px = 0; px < W; ++px)
        {
            const float y = juce::jlimit (r.getY(), r.getBottom(), yForSpec (s[(size_t) px]));
            if (px == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + (float) px, y);
        }
        if (fill) { p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath(); }
        return p;
    };
    if (! specPre.empty()) { g.setColour (juce::Colour (0x40ffffff)); g.strokePath (specPath (specPre, false), juce::PathStrokeType (1.0f)); }
    if (! specPost.empty())
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x3a5ec8f2), 0, r.getY(), juce::Colour (0x085ec8f2), 0, r.getBottom(), false));
        g.fillPath (specPath (specPost, true));
        g.setColour (juce::Colour (0x605ec8f2)); g.strokePath (specPath (specPost, false), juce::PathStrokeType (1.0f));
    }

    // curves
    const int NP = juce::jmax (64, (int) r.getWidth() / 2);
    std::vector<double> total ((size_t) NP, 0.0);
    std::vector<ASec> secs;
    for (int i = 0; i < NBANDS; ++i)
    {
        BandParams b; proc.readBand (i, b);
        if (! b.used) continue;
        BandSpec s = b.spec;
        if (b.dyn) s.gainDb += proc.bandUiOffset (i);
        designBand (s, secs);
        juce::Path p; std::vector<float> ys ((size_t) NP);
        for (int k = 0; k < NP; ++k)
        {
            const float x = r.getX() + r.getWidth() * k / (NP - 1);
            const double db = secs.empty() ? 0.0 : 20.0 * std::log10 (std::abs (analogResponse (secs, fForX (x))) + 1e-9);
            if (! b.bypass) total[(size_t) k] += db;
            ys[(size_t) k] = juce::jlimit (r.getY(), r.getBottom(), yForDb (db));
            if (k == 0) p.startNewSubPath (x, ys[(size_t) k]); else p.lineTo (x, ys[(size_t) k]);
        }
        const auto col = bandColour (i);
        if (i == selected)
        {
            juce::Path f (p); f.lineTo (r.getRight(), yForDb (0)); f.lineTo (r.getX(), yForDb (0)); f.closeSubPath();
            g.setColour (col.withAlpha (0.22f)); g.fillPath (f);
            g.setColour (col.withAlpha (0.9f)); g.strokePath (p, juce::PathStrokeType (1.5f));
        }
        else { g.setColour (col.withAlpha (0.35f)); g.strokePath (p, juce::PathStrokeType (1.0f)); }
    }
    juce::Path tp;
    for (int k = 0; k < NP; ++k)
    {
        const float x = r.getX() + r.getWidth() * k / (NP - 1), y = juce::jlimit (r.getY(), r.getBottom(), yForDb (total[(size_t) k]));
        if (k == 0) tp.startNewSubPath (x, y); else tp.lineTo (x, y);
    }
    g.setColour (CURVE); g.strokePath (tp, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // nodes
    for (int i = 0; i < NBANDS; ++i)
    {
        BandParams b; proc.readBand (i, b);
        if (! b.used) continue;
        auto pt = nodePos (i, b);
        const auto col = bandColour (i);
        const float rad = (i == selected || i == hover) ? 9.0f : 7.5f;
        if (b.dyn && hasGain (b.spec.type))
        {   // dynamic range bar + live position
            const float y1 = yForDb (b.spec.gainDb + b.range * (b.learned ? proc.state.getRawParameterValue ("strength")->load() / 100.0f : 1.0f));
            g.setColour (col.withAlpha (0.5f));
            g.drawLine (pt.x, pt.y, pt.x, y1, 2.0f);
            g.fillEllipse (pt.x - 3, y1 - 3, 6, 6);
            const float yl = yForDb (b.spec.gainDb + proc.bandUiOffset (i));
            g.setColour (col); g.drawEllipse (pt.x - 5, yl - 5, 10, 10, 1.5f);
        }
        g.setColour (col.withAlpha (b.bypass ? 0.3f : 0.95f)); g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
        const bool gOn = b.spec.gChar && gCapable (b.spec.type);
        if (gOn) { g.setColour (GOLD); g.drawEllipse (pt.x - rad - 2.5f, pt.y - rad - 2.5f, (rad + 2.5f) * 2, (rad + 2.5f) * 2, 1.6f); }
        if (b.learned)
        {
            juce::Path c; c.addEllipse (pt.x - rad - 5, pt.y - rad - 5, (rad + 5) * 2, (rad + 5) * 2);
            juce::Path d; const float dash[] = { 3.0f, 3.0f }; juce::PathStrokeType (1.0f).createDashedStroke (d, c, dash, 2);
            g.setColour (col); g.fillPath (d);
        }
        g.setColour (juce::Colours::black); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2), juce::Justification::centred);
        // tag: G (SSL) or C (clean because the SSL has no such filter)
        if (b.spec.gChar)
        {
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.setColour (gOn ? GOLD : DIM);
            g.drawText (gOn ? "G" : "C", juce::Rectangle<float> (pt.x + rad, pt.y - rad - 10, 12, 10), juce::Justification::centredLeft);
        }
    }
    // hover readout
    const int show = dragging >= 0 ? dragging : (hover >= 0 ? hover : -1);
    if (show >= 0)
    {
        BandParams b; proc.readBand (show, b);
        juce::String t = juce::String (TYPE_NAMES[(int) b.spec.type]) + "  " +
                         (b.spec.freq >= 1000 ? juce::String (b.spec.freq / 1000.0, 2) + " kHz" : juce::String (b.spec.freq, 1) + " Hz");
        if (hasGain (b.spec.type)) t << "  " << (b.spec.gainDb > 0 ? "+" : "") << juce::String (b.spec.gainDb, 1) << " dB";
        t << "  Q " << juce::String (b.spec.q, 2);
        if (b.spec.gChar && ! gCapable (b.spec.type)) t << "  (Clean - no SSL version)";
        auto pt = nodePos (show, b);
        auto box = juce::Rectangle<float> (pt.x + 14, pt.y - 30, 260, 18);
        if (box.getRight() > r.getRight()) box.setX (pt.x - 274);
        if (box.getY() < r.getY()) box.setY (pt.y + 14);
        g.setColour (juce::Colour (0xe0181a20)); g.fillRoundedRectangle (box, 4);
        g.setColour (TEXT); g.setFont (juce::FontOptions (11.5f)); g.drawText (t, box.reduced (6, 0), juce::Justification::centredLeft);
    }
    if (proc.findFreeBand() == 0 && dragging < 0)
    {
        g.setColour (DIM); g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Double-click to add a band  -  right-click a point for Type / Order / Key", r, juce::Justification::centred);
    }
}

void Graph::addBandAt (juce::Point<float> p)
{
    const int i = proc.findFreeBand();
    if (i < 0) return;
    proc.resetBand (i);
    const double f = fForX (p.x);
    Type t = Type::Peak;
    if (f < 30) t = Type::HighPass; else if (f > 17000) t = Type::LowPass;
    setP (i, "type", (float) t);
    setP (i, "freq", (float) f);
    setP (i, "gain", hasGain (t) ? (float) juce::jlimit (-21.5, 21.5, dbForY (p.y)) : 0.0f);
    setP (i, "char", 1.0f);
    setP (i, "used", 1.0f);
    selected = i; if (onSelect) onSelect (i);
}

void Graph::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (e.mods.isPopupMenu())
    {
        if (h >= 0) { selected = h; if (onSelect) onSelect (h); showBandMenu (h); }
        else
        {
            juce::PopupMenu m; m.addItem (1, "Add band here");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, pos = e.position] (int r) { if (r == 1) addBandAt (pos); });
        }
        return;
    }
    if (h >= 0)
    {
        selected = h; dragging = h; dragStart = e.position;
        startF = getP (h, "freq"); startG = getP (h, "gain"); startQ = getP (h, "q");
        if (auto* p = proc.bandParam (h, "freq")) p->beginChangeGesture();
        if (auto* p = proc.bandParam (h, "gain")) p->beginChangeGesture();
        if (onSelect) onSelect (h);
    }
    else { selected = -1; if (onSelect) onSelect (-1); }
    repaint();
}

void Graph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const auto d = (e.position - dragStart) * fine;
    const double f = juce::jlimit (10.0, 30000.0, fForX (xForF (startF) + d.x));
    BandParams b; proc.readBand (dragging, b);
    const bool gOn = b.spec.gChar && gCapable (b.spec.type);
    auto setRaw = [&] (const char* fld, float v) { if (auto* p = proc.bandParam (dragging, fld)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    setRaw ("freq", (float) f);
    if (hasGain (b.spec.type))
    {
        const double lim = gOn ? G_MAX_DB : CLEAN_MAX_DB;
        setRaw ("gain", (float) juce::jlimit (-lim, lim, dbForY (yForDb (startG) + d.y)));
    }
    else if (b.spec.type == Type::BandPass || b.spec.type == Type::BandStop)
        setRaw ("q", (float) juce::jlimit (0.025, 100.0, startQ * std::pow (2.0, -d.y / 40.0)));
    repaint();
}

void Graph::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        if (auto* p = proc.bandParam (dragging, "freq")) p->endChangeGesture();
        if (auto* p = proc.bandParam (dragging, "gain")) p->endChangeGesture();
    }
    dragging = -1;
}

void Graph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h < 0) addBandAt (e.position);
}

void Graph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int h = hitNode (e.position);
    if (h < 0) h = selected;
    if (h < 0) return;
    BandParams b; proc.readBand (h, b);
    const float dir = w.deltaY > 0 ? 1.0f : -1.0f;
    if ((b.spec.type == Type::HighPass || b.spec.type == Type::LowPass) && ! (b.spec.gChar))
    {   // pass filters: wheel changes the slope (Order), like Pro-Q
        const int o = juce::jlimit (0, 9, (int) b.spec.order + (int) dir);
        setP (h, "order", (float) o);
    }
    else setP (h, "q", (float) juce::jlimit (0.025, 100.0, b.spec.q * std::pow (1.12, (double) dir)));
    repaint();
}

void Graph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void Graph::showBandMenu (int band)
{
    BandParams b; proc.readBand (band, b);
    juce::PopupMenu m, mt, mo, mk, mc, ms, mst;
    m.addSectionHeader ("Band " + juce::String (band + 1));
    m.addItem (1, "Reset");
    for (int t = 0; t < 8; ++t) mt.addItem (100 + t, TYPE_NAMES[t], true, (int) b.spec.type == t);
    m.addSubMenu ("Type", mt);
    const bool gPass = b.spec.gChar && (b.spec.type == Type::HighPass || b.spec.type == Type::LowPass);
    for (int o = 0; o < 10; ++o) mo.addItem (200 + o, ORDER_NAMES[o], ! gPass, (int) b.spec.order == o);
    m.addSubMenu (gPass ? "Order (fixed by SSL)" : "Order", mo);
    const char* notes[] = { "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#" };
    const int semiFromC[] = { 9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int n = 0; n < 12; ++n)
    {
        juce::PopupMenu oct;
        for (int o = 0; o <= 8; ++o)
        {
            const double f = 440.0 * std::pow (2.0, (semiFromC[n] - 9 + 12 * (o - 4)) / 12.0);
            if (f >= 10 && f <= 22000) oct.addItem (300 + n * 10 + o, juce::String (notes[n]) + juce::String (o) + "   " + juce::String (f, 1) + " Hz");
        }
        mk.addSubMenu (notes[n], oct);
    }
    m.addSubMenu ("Key", mk);
    m.addSeparator();
    mc.addItem (400, "Clean (FabFilter-accurate)", true, ! b.spec.gChar);
    mc.addItem (401, "G (measured SSL)", true, b.spec.gChar);
    m.addSubMenu ("Character", mc);
    for (int s = 0; s < 5; ++s) ms.addItem (500 + s, juce::String::fromUTF8 (SHAPE_NAMES[s]), b.spec.gChar && b.spec.type == Type::Peak, (int) b.spec.shape == s);
    m.addSubMenu ("SSL shape", ms, b.spec.gChar && b.spec.type == Type::Peak);
    for (int s = 0; s < 5; ++s) mst.addItem (600 + s, STEREO_NAMES[s], true, (int) b.place == s);
    m.addSubMenu ("Stereo", mst);
    m.addItem (700, "Dynamic", true, b.dyn);
    m.addSeparator();
    m.addItem (800, "Delete band");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, band] (int r)
    {
        if (r == 0) return;
        if (r == 1) { const float f = getP (band, "freq"); proc.resetBand (band); setP (band, "freq", f); setP (band, "used", 1.0f); }
        else if (r >= 100 && r < 108) setP (band, "type", (float) (r - 100));
        else if (r >= 200 && r < 210) setP (band, "order", (float) (r - 200));
        else if (r >= 300 && r < 420 && r < 400)
        {
            const int n = (r - 300) / 10, o = (r - 300) % 10;
            const int semiFromC[] = { 9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
            setP (band, "freq", (float) (440.0 * std::pow (2.0, (semiFromC[n] - 9 + 12 * (o - 4)) / 12.0)));
        }
        else if (r == 400 || r == 401) setP (band, "char", (float) (r - 400));
        else if (r >= 500 && r < 505) setP (band, "shape", (float) (r - 500));
        else if (r >= 600 && r < 605) setP (band, "stereo", (float) (r - 600));
        else if (r == 700) setP (band, "dyn", getP (band, "dyn") > 0.5f ? 0.0f : 1.0f);
        else if (r == 800) { proc.resetBand (band); selected = -1; if (onSelect) onSelect (-1); }
        repaint();
        if (onSelect) onSelect (selected);
    });
}
} // namespace mxui

// ================= Editor =================
MexEditor::MexEditor (MexProcessor& p) : AudioProcessorEditor (&p), proc (p), graph (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (graph);
    graph.onSelect = [this] (int i) { selectBand (i); };

    for (auto* b : { &liveBtn, &hqBtn, &autoBtn })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, GOLD);
    }
    liveBtn.onClick = [this] { proc.setParam ("quality", 0); };
    hqBtn.onClick = [this] { proc.setParam ("quality", 1); };
    autoBtn.onClick = [this] { proc.setParam ("quality", 2); };
    liveBtn.setTooltip ("Zero latency - track vocals through it");
    hqBtn.setTooltip ("Natural or Linear phase, oversampled - for mixing and bouncing");
    autoBtn.setTooltip ("LIVE while you play, HQ automatically when FL exports");

    phaseBox.addItemList ({ "Natural phase", "Linear Low", "Linear Medium", "Linear High", "Linear Max" }, 1);
    addAndMakeVisible (phaseBox);
    aPhase = std::make_unique<CA> (proc.state, "phase", phaseBox);

    learnBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff7a9c));
    learnBtn.setTooltip ("Play 10-30 s of the track, then press again: finds problem frequencies and adds dynamic cuts");
    learnBtn.onClick = [this]
    {
        if (proc.isLearning()) proc.finishLearn(); else proc.startLearn();
        refreshPanel();
    };
    addAndMakeVisible (learnBtn);

    analyzerBox.addItemList ({ "Analyzer: off", "Analyzer: post", "Analyzer: pre+post" }, 1);
    analyzerBox.setSelectedItemIndex (1, juce::dontSendNotification);
    analyzerBox.onChange = [this] { graph.analyzerMode = analyzerBox.getSelectedItemIndex(); };
    addAndMakeVisible (analyzerBox);

    for (auto* k : { &drive, &output, &strength }) addAndMakeVisible (*k);
    aDrive = std::make_unique<SA> (proc.state, "drive", drive.slider);
    aOut = std::make_unique<SA> (proc.state, "output", output.slider);
    aStrength = std::make_unique<SA> (proc.state, "strength", strength.slider);
    drive.slider.setTooltip ("Noise-free console saturation - no hiss, silence stays silent");
    strength.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffff7a9c));

    modeLabel.setFont (juce::FontOptions (11.5f)); modeLabel.setColour (juce::Label::textColourId, DIM);
    addAndMakeVisible (modeLabel);
    statusLabel.setFont (juce::FontOptions (12.0f)); statusLabel.setColour (juce::Label::textColourId, DIM);
    addAndMakeVisible (statusLabel);

    // band panel
    bandTitle.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (bandTitle);
    typeBox.addItemList ({ "Disabled", "Low pass", "Band pass", "High pass", "Band stop", "Low shelf", "Peaking", "High shelf" }, 1);
    orderBox.addItemList ({ "Gentle 8", "Gentle 6", "Gentle 4", "2", "Steep 4", "Steep 6", "Steep 8", "Steep 12", "Steep 16", "Brickwall" }, 1);
    charBox.addItemList ({ "Clean", "G (SSL)" }, 1);
    for (auto* s : SHAPE_NAMES) shapeBox.addItem (juce::String::fromUTF8 (s), shapeBox.getNumItems() + 1);
    stereoBox.addItemList ({ "Stereo", "Left", "Right", "Mid", "Side" }, 1);
    for (auto* c : { &typeBox, &orderBox, &charBox, &shapeBox, &stereoBox }) addAndMakeVisible (*c);
    for (auto* k : { &freqK, &gainK, &qK, &rangeK, &thrK, &attK, &relK }) addAndMakeVisible (*k);
    for (auto* t : { &dynBtn, &autoThrBtn, &scBtn }) addAndMakeVisible (*t);
    delBtn.onClick = [this] { if (panelBand >= 0) { proc.resetBand (panelBand); graph.selected = -1; selectBand (-1); } };
    addAndMakeVisible (delBtn);

    setResizable (true, true);
    setResizeLimits (820, 540, 1800, 1100);
    setSize (1040, 640);
    selectBand (-1);
    startTimerHz (30);
}

MexEditor::~MexEditor()
{
    stopTimer();
    aFreq.reset(); aGain.reset(); aQ.reset(); aRange.reset(); aThr.reset(); aAtt.reset(); aRel.reset();
    aType.reset(); aOrder.reset(); aChar.reset(); aShape.reset(); aStereo.reset(); aDyn.reset(); aAutoThr.reset(); aSc.reset();
    aDrive.reset(); aOut.reset(); aStrength.reset(); aPhase.reset();
    setLookAndFeel (nullptr);
}

void MexEditor::selectBand (int i)
{
    if (i == panelBand) { refreshPanel(); return; }
    panelBand = i;
    aFreq.reset(); aGain.reset(); aQ.reset(); aRange.reset(); aThr.reset(); aAtt.reset(); aRel.reset();
    aType.reset(); aOrder.reset(); aChar.reset(); aShape.reset(); aStereo.reset(); aDyn.reset(); aAutoThr.reset(); aSc.reset();
    if (i >= 0)
    {
        auto id = [i] (const char* f) { return MexProcessor::pid (i, f); };
        aFreq = std::make_unique<SA> (proc.state, id ("freq"), freqK.slider);
        aGain = std::make_unique<SA> (proc.state, id ("gain"), gainK.slider);
        aQ = std::make_unique<SA> (proc.state, id ("q"), qK.slider);
        aRange = std::make_unique<SA> (proc.state, id ("range"), rangeK.slider);
        thrBoundAuto = -1;   // bound in refreshPanel (threshold or auto offset)
        aAtt = std::make_unique<SA> (proc.state, id ("attack"), attK.slider);
        aRel = std::make_unique<SA> (proc.state, id ("release"), relK.slider);
        aType = std::make_unique<CA> (proc.state, id ("type"), typeBox);
        aOrder = std::make_unique<CA> (proc.state, id ("order"), orderBox);
        aChar = std::make_unique<CA> (proc.state, id ("char"), charBox);
        aShape = std::make_unique<CA> (proc.state, id ("shape"), shapeBox);
        aStereo = std::make_unique<CA> (proc.state, id ("stereo"), stereoBox);
        aDyn = std::make_unique<BA> (proc.state, id ("dyn"), dynBtn);
        aAutoThr = std::make_unique<BA> (proc.state, id ("autothr"), autoThrBtn);
        aSc = std::make_unique<BA> (proc.state, id ("sc"), scBtn);
        const auto col = bandColour (i);
        for (auto* k : { &freqK, &gainK, &qK }) k->slider.setColour (juce::Slider::rotarySliderFillColourId, col);
    }
    refreshPanel();
    resized();
}

void MexEditor::refreshPanel()
{
    const bool has = panelBand >= 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &typeBox, &orderBox, &charBox, &shapeBox, &stereoBox, &freqK, &gainK, &qK,
                                                                        &dynBtn, &delBtn, &rangeK, &thrK, &attK, &relK, &autoThrBtn, &scBtn })
        c->setVisible (has);
    if (has)
    {
        BandParams b; proc.readBand (panelBand, b);
        bandTitle.setText ("BAND " + juce::String (panelBand + 1) + (b.learned ? "  (learned)" : ""), juce::dontSendNotification);
        bandTitle.setColour (juce::Label::textColourId, bandColour (panelBand));
        const bool dyn = b.dyn;
        for (juce::Component* c : std::initializer_list<juce::Component*> { &rangeK, &thrK, &attK, &relK, &autoThrBtn, &scBtn }) c->setEnabled (dyn);
        if ((int) b.autoThr != thrBoundAuto)
        {
            aThr.reset();
            aThr = std::make_unique<SA> (proc.state, MexProcessor::pid (panelBand, b.autoThr ? "toff" : "thr"), thrK.slider);
            thrK.label.setText (b.autoThr ? "ABOVE AVG" : "THRESH", juce::dontSendNotification);
            thrK.slider.setTooltip (b.autoThr ? "Auto threshold: dB above the band's own average level (negative = always working)" : "Fixed threshold (dBFS)");
            thrBoundAuto = (int) b.autoThr;
        }
        shapeBox.setEnabled (b.spec.gChar && b.spec.type == Type::Peak);
        orderBox.setEnabled (! (b.spec.gChar && (b.spec.type == Type::HighPass || b.spec.type == Type::LowPass)));
        gainK.setEnabled (b.spec.type == Type::Peak || b.spec.type == Type::LowShelf || b.spec.type == Type::HighShelf);
    }
    else
    {
        bandTitle.setText ("No band selected", juce::dontSendNotification);
        bandTitle.setColour (juce::Label::textColourId, DIM);
    }
}

void MexEditor::timerCallback()
{
    const int q = (int) proc.state.getRawParameterValue ("quality")->load();
    liveBtn.setToggleState (q == 0, juce::dontSendNotification);
    hqBtn.setToggleState (q == 1, juce::dontSendNotification);
    autoBtn.setToggleState (q == 2, juce::dontSendNotification);
    phaseBox.setEnabled (q == 1);
    const int lat = proc.getLatencySamples();
    modeLabel.setText (proc.modeName() + "  |  latency " + juce::String (lat) + " smp (" + juce::String (lat * 1000.0 / juce::jmax (1.0, proc.getSr()), 1) + " ms)",
                       juce::dontSendNotification);
    const bool learning = proc.isLearning();
    learnBtn.setToggleState (learning, juce::dontSendNotification);
    learnBtn.setButtonText (learning ? "LEARNING " + juce::String ((int) proc.learnSeconds()) + " s - press to finish" : "LEARN");
    statusLabel.setText (proc.lastLearnSummary.isNotEmpty() ? proc.lastLearnSummary
                                                            : "Double-click the graph to add a band. Right-click a point: Reset / Type / Order / Key / Character / SSL shape / Stereo / Dynamic.",
                         juce::dontSendNotification);
    if (! learning && learnBtn.getButtonText() != "LEARN") learnBtn.setButtonText ("LEARN");
    graph.updateAnalyzer();
    refreshPanel();
    graph.repaint();
}

void MexEditor::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    auto top = getLocalBounds().removeFromTop (TOP);
    g.setColour (PANEL); g.fillRect (top);
    g.setColour (CURVE); g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MEX-EQ", 16, 10, 120, 30, juce::Justification::centredLeft);
    g.setColour (GOLD); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("G-SERIES MEASURED", 16, 40, 140, 14, juce::Justification::centredLeft);
    auto panel = getLocalBounds().removeFromBottom (134);
    g.setColour (PANEL); g.fillRect (panel);
    g.setColour (GRID); g.drawHorizontalLine (panel.getY(), 0, (float) getWidth());
}

void MexEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (TOP).reduced (8, 4);
    top.removeFromLeft (150);
    auto midY = [] (juce::Rectangle<int> b, int h) { return b.withSizeKeepingCentre (b.getWidth(), h); };
    for (auto* b : { &liveBtn, &hqBtn, &autoBtn }) b->setBounds (midY (top.removeFromLeft (60), 30).reduced (2, 0));
    top.removeFromLeft (6);
    phaseBox.setBounds (midY (top.removeFromLeft (130), 26).reduced (2, 0));
    top.removeFromLeft (8);
    learnBtn.setBounds (midY (top.removeFromLeft (220), 30).reduced (2, 0));
    strength.setBounds (top.removeFromLeft (72));
    output.setBounds (top.removeFromRight (72));
    drive.setBounds (top.removeFromRight (72));
    analyzerBox.setBounds (midY (top.removeFromRight (196), 26).reduced (4, 0));

    auto panel = r.removeFromBottom (134).reduced (10, 6);
    auto status = r.removeFromBottom (20);
    statusLabel.setBounds (status.reduced (10, 0));
    modeLabel.setBounds (getWidth() - 430, TOP + 2, 420, 16);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    graph.setBounds (r.reduced (8, 4));

    auto left = panel.removeFromLeft (190);
    bandTitle.setBounds (left.removeFromTop (22));
    typeBox.setBounds (left.removeFromTop (24).reduced (0, 1));
    orderBox.setBounds (left.removeFromTop (24).reduced (0, 1));
    stereoBox.setBounds (left.removeFromTop (24).reduced (0, 1));
    auto col2 = panel.removeFromLeft (150).reduced (6, 0);
    col2.removeFromTop (22);
    charBox.setBounds (col2.removeFromTop (24).reduced (0, 1));
    shapeBox.setBounds (col2.removeFromTop (24).reduced (0, 1));
    delBtn.setBounds (col2.removeFromTop (26).reduced (0, 2));
    for (auto* k : { &freqK, &gainK, &qK }) k->setBounds (panel.removeFromLeft (84));
    panel.removeFromLeft (14);
    auto dynCol = panel.removeFromLeft (96);
    dynBtn.setBounds (dynCol.removeFromTop (28));
    autoThrBtn.setBounds (dynCol.removeFromTop (28));
    scBtn.setBounds (dynCol.removeFromTop (28));
    for (auto* k : { &rangeK, &thrK, &attK, &relK }) k->setBounds (panel.removeFromLeft (80));
}
