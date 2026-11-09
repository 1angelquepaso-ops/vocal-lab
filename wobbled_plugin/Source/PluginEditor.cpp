#include "PluginEditor.h"

using namespace wbui;
namespace id = wb::id;

// ====================================================================== look
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::ComboBox::backgroundColourId, col::panel2);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::arrowColourId, col::lime);
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::lime.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::panel2);
    setColour (juce::TextButton::buttonOnColourId, col::lime.darker (0.35f));
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel2);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::lime.withAlpha (0.5f));
    setColour (juce::BubbleComponent::backgroundColourId, col::panel2);
    setColour (juce::BubbleComponent::outlineColourId, col::lime.withAlpha (0.6f));
    setColour (juce::Label::textColourId, col::text);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const juce::Colour accent = s.getProperties().contains ("accent") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["accent"]) : col::lime;

    g.setColour (col::panel2);
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    juce::Path track;
    track.addCentredArc (c.x, c.y, r * 0.88f, r * 0.88f, 0, a0, a1, true);
    g.setColour (col::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    const float to = a0 + pos * (a1 - a0);
    juce::Path val;
    val.addCentredArc (c.x, c.y, r * 0.88f, r * 0.88f, 0, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ang = to - juce::MathConstants<float>::halfPi;
    g.setColour (col::text);
    g.drawLine (c.x + std::cos (ang) * r * 0.2f, c.y + std::sin (ang) * r * 0.2f,
                c.x + std::cos (ang) * r * 0.62f, c.y + std::sin (ang) * r * 0.62f, 2.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const juce::Colour on = b.getProperties().contains ("accent") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["accent"]) : col::lime;
    juce::Colour fill = b.getToggleState() ? on.withAlpha (0.85f) : col::panel2;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.getToggleState() ? on : col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? col::lime : col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path tri;
    const float cx = (float) w - 10.0f, cy = (float) h * 0.5f;
    tri.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (col::lime);
    g.fillPath (tri);
}

// ====================================================================== host (FL) parameter menu
bool wbui::showHostMenu (juce::Component& from, juce::AudioProcessorParameter* param, juce::Point<int> screenPos)
{
    if (param == nullptr) return false;
    auto* ed = from.findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (ed == nullptr) return false;
    if (auto* ctx = ed->getHostContext())
        if (auto menu = ctx->getContextMenuForParameter (param))
        {
            menu->showNativeMenu (ed->getLocalPoint (nullptr, screenPos));
            return true;
        }
    // host has no menu (e.g. Standalone): offer a reset
    juce::PopupMenu m;
    m.addItem (1, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [param] (int r)
                     {
                         if (r != 1) return;
                         param->beginChangeGesture();
                         param->setValueNotifyingHost (param->getDefaultValue());
                         param->endChangeGesture();
                     });
    return false;
}

// ====================================================================== small controls
static void drawCaption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (t, r, juce::Justification::centred, 1);
}

Knob::Knob (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& cap, const juce::String& help)
    : caption (cap)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setTooltip (help);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, paramId, slider);
    slider.param = st.getParameter (paramId);
    if (auto* p = st.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}
void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (14).reduced (2)); }
void Knob::paint (juce::Graphics& g) { drawCaption (g, getLocalBounds().removeFromBottom (14), caption); }

Choice::Choice (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& cap, const juce::String& help)
    : caption (cap)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    box.setTooltip (help);
    addAndMakeVisible (box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, paramId, box);
    box.param = st.getParameter (paramId);
}
void Choice::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (14);
    box.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 6, 110), 24));
}
void Choice::paint (juce::Graphics& g) { drawCaption (g, getLocalBounds().removeFromBottom (14), caption); }

Toggle::Toggle (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& text, const juce::String& cap, const juce::String& help)
    : caption (cap)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    button.setTooltip (help);
    addAndMakeVisible (button);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, paramId, button);
    button.param = st.getParameter (paramId);
}
void Toggle::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (14);
    button.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 72), 24));
}
void Toggle::paint (juce::Graphics& g) { drawCaption (g, getLocalBounds().removeFromBottom (14), caption); }

// ====================================================================== sample view
void SampleView::refresh()
{
    const auto* s = proc.sampleForDisplay();
    const float st = proc.state.getRawParameterValue (id::smpStart)->load();
    const float ln = proc.state.getRawParameterValue (id::smpLength)->load();
    const bool lp = proc.state.getRawParameterValue (id::smpLoop)->load() > 0.5f;
    const bool rv = proc.state.getRawParameterValue (id::smpReverse)->load() > 0.5f;
    const int src = (int) proc.state.getRawParameterValue (id::source)->load();
    if (s != shown || (int) peaks.size() != getWidth())
    {
        shown = s;
        peaks.clear();
        if (s != nullptr && getWidth() > 0)
        {
            const int w = getWidth(), n = s->buf.getNumSamples();
            peaks.resize ((size_t) w);
            for (int x = 0; x < w; ++x)
            {
                const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
                float lo = 0, hi = 0;
                for (int i = a; i < b && i < n; ++i)
                {
                    const float v = 0.5f * (s->buf.getSample (0, i) + s->buf.getSample (1, i));
                    lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
                }
                peaks[(size_t) x] = { lo, hi };
            }
        }
        repaint();
    }
    if (st != lastStart || ln != lastLen || lp != lastLoop || rv != lastRev || src != lastSrc)
    {
        lastStart = st; lastLen = ln; lastLoop = lp; lastRev = rv; lastSrc = src;
        repaint();
    }
}

void SampleView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const int src = (int) proc.state.getRawParameterValue (id::source)->load();

    if (shown == nullptr)
    {
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Drop a sample here (or click)  -  WAV, AIFF, FLAC, MP3", getLocalBounds().withTrimmedBottom (20), juce::Justification::centred, 1);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (col::lime.withAlpha (0.8f));
        g.drawFittedText ("Playing built-in tone: " + wb::sourceNames[src], getLocalBounds().withTrimmedTop (34), juce::Justification::centred, 1);
        return;
    }
    const float midY = r.getCentreY(), hh = r.getHeight() * 0.42f;
    const bool active = src == 0;
    g.setColour ((active ? col::lime : col::dim).withAlpha (0.75f));
    for (int x = 0; x < (int) peaks.size(); ++x)
        g.drawVerticalLine (x, midY - peaks[(size_t) x].second * hh, midY - peaks[(size_t) x].first * hh + 1.0f);

    const float a = lastStart, len = lastLen * (1.0f - a);
    const float xa = r.getX() + a * r.getWidth(), xb = r.getX() + (a + len) * r.getWidth();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), xa - r.getX(), r.getHeight()));
    g.fillRect (juce::Rectangle<float> (xb, r.getY(), r.getRight() - xb, r.getHeight()));
    g.setColour (col::lime);
    g.drawLine (xa, r.getY() + 2, xa, r.getBottom() - 2, 2.0f);
    g.drawLine (xb, r.getY() + 2, xb, r.getBottom() - 2, 2.0f);

    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (col::text);
    juce::String info = shown->name;
    if (shown->detectedNote > 0) info << "   root " << juce::MidiMessage::getMidiNoteName ((int) std::lround (shown->detectedNote), true, true, 3);
    if (! active) info << "   (Source is " << wb::sourceNames[src] << " - pick Sample to hear it)";
    g.drawText (info, getLocalBounds().reduced (8, 4).removeFromTop (16), juce::Justification::left);
    juce::String tags;
    if (lastLoop) tags << "LOOP ";
    if (lastRev) tags << "REVERSE";
    g.setColour (col::lime);
    g.drawText (tags, getLocalBounds().reduced (8, 4).removeFromTop (16), juce::Justification::right);
}

void SampleView::mouseUp (const juce::MouseEvent&) { if (onClickLoad) onClickLoad(); }

// ====================================================================== LFO view
juce::Rectangle<float> LfoView::strip() const { return getLocalBounds().toFloat().removeFromTop (18.0f).reduced (8.0f, 0.0f); }
juce::Rectangle<float> LfoView::graph() const { return getLocalBounds().toFloat().withTrimmedTop (22.0f).reduced (8.0f, 6.0f); }
juce::Point<float> LfoView::toScreen (float x, float y) const
{
    const auto g = graph();
    return { g.getX() + x * g.getWidth(), g.getBottom() - y * g.getHeight() };
}
float LfoView::snapX (float x, bool free) const
{
    const int grid = proc.shape.grid;
    if (! free) x = std::round (x * grid) / (float) grid;
    return juce::jlimit (0.0f, 0.999f, x);
}
int LfoView::hitPoint (juce::Point<float> p) const
{
    for (int i = 0; i < (int) proc.shape.pts.size(); ++i)
        if (toScreen (proc.shape.pts[(size_t) i].x, proc.shape.pts[(size_t) i].y).getDistanceFrom (p) < 8.0f) return i;
    return -1;
}
int LfoView::hitCurve (juce::Point<float> p) const
{
    const auto& pts = proc.shape.pts;
    const int n = (int) pts.size();
    for (int i = 0; i < n; ++i)
    {
        if (pts[(size_t) i].seg != wb::Seg::curve || n < 2) continue;
        const float bx = i + 1 < n ? pts[(size_t) i + 1].x : pts[0].x + 1.0f;
        float mx = 0.5f * (pts[(size_t) i].x + bx);
        if (mx >= 1.0f) mx -= 1.0f;
        if (toScreen (mx, proc.shape.valueAt (mx)).getDistanceFrom (p) < 7.0f) return i;
    }
    return -1;
}
void LfoView::changed() { proc.shapeChanged(); repaint(); }

void LfoView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (all, 6.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    const auto gr = graph();
    const auto& sh = proc.shape;

    // grid
    for (int k = 0; k <= sh.grid; ++k)
    {
        const float x = gr.getX() + gr.getWidth() * k / (float) sh.grid;
        const bool major = (sh.grid % 4 == 0) ? (k % (sh.grid / 4) == 0) : (k % (sh.grid / 3 > 0 ? sh.grid / 3 : 1) == 0);
        g.setColour (major ? col::line.brighter (0.25f) : col::line);
        g.drawVerticalLine ((int) x, gr.getY(), gr.getBottom());
    }
    for (int k = 0; k <= 4; ++k)
    {
        g.setColour (k == 2 ? col::line.brighter (0.2f) : col::line);
        g.drawHorizontalLine ((int) (gr.getY() + gr.getHeight() * k / 4.0f), gr.getX(), gr.getRight());
    }
    // outside loop region
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    const float lx = gr.getX() + sh.loopStart * gr.getWidth(), rx = gr.getX() + sh.loopEnd * gr.getWidth();
    g.fillRect (juce::Rectangle<float> (gr.getX(), gr.getY(), lx - gr.getX(), gr.getHeight()));
    g.fillRect (juce::Rectangle<float> (rx, gr.getY(), gr.getRight() - rx, gr.getHeight()));

    // shape
    juce::Path p, fill;
    const int W = juce::jmax (2, (int) gr.getWidth());
    for (int i = 0; i <= W; ++i)
    {
        const float x = (float) i / (float) W;
        const auto pt = toScreen (x, sh.valueAt (juce::jmin (x, 0.99999f)));
        if (i == 0) { p.startNewSubPath (pt); fill.startNewSubPath (gr.getX(), gr.getBottom()); }
        p.lineTo (pt); fill.lineTo (pt);
    }
    fill.lineTo (gr.getRight(), gr.getBottom()); fill.closeSubPath();
    g.setColour (col::lime.withAlpha (0.12f)); g.fillPath (fill);
    g.setColour (col::lime); g.strokePath (p, juce::PathStrokeType (2.0f));

    // points + curve handles
    const int n = (int) sh.pts.size();
    for (int i = 0; i < n; ++i)
    {
        const auto& pt = sh.pts[(size_t) i];
        const auto s = toScreen (pt.x, pt.y);
        g.setColour (pt.seg == wb::Seg::hold ? col::sub : (pt.seg == wb::Seg::smooth ? col::mag : col::lime));
        g.fillEllipse (s.x - 5, s.y - 5, 10, 10);
        g.setColour (col::bg); g.drawEllipse (s.x - 5, s.y - 5, 10, 10, 1.5f);
        if (pt.seg == wb::Seg::curve && n > 1)
        {
            const float bx = i + 1 < n ? sh.pts[(size_t) i + 1].x : sh.pts[0].x + 1.0f;
            float mx = 0.5f * (pt.x + bx); if (mx >= 1.0f) mx -= 1.0f;
            const auto m = toScreen (mx, sh.valueAt (mx));
            g.setColour (col::text.withAlpha (0.6f));
            g.drawEllipse (m.x - 3.5f, m.y - 3.5f, 7, 7, 1.2f);
        }
    }
    // loop markers
    const auto st = strip();
    g.setColour (col::lime.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (lx, st.getY() + 6, rx - lx, 6));
    for (float mx : { lx, rx })
    {
        juce::Path t; t.addTriangle (mx - 6, st.getY() + 2, mx + 6, st.getY() + 2, mx, st.getBottom());
        g.setColour (col::lime); g.fillPath (t);
    }
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("LOOP", juce::Rectangle<float> (lx + 8, st.getY(), 60, st.getHeight()), juce::Justification::left);

    // playhead
    if (lastPos >= 0)
    {
        const float x = gr.getX() + lastPos * gr.getWidth();
        g.setColour (col::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, gr.getY(), gr.getBottom());
        const auto d = toScreen (lastPos, lastVal);
        g.setColour (col::text);
        g.fillEllipse (d.x - 4, d.y - 4, 8, 8);
    }
}

void LfoView::tick()
{
    const float pos = proc.engine.dispPos.load(), val = proc.engine.dispValue.load();
    if (std::abs (pos - lastPos) > 1e-4f || std::abs (val - lastVal) > 1e-4f) { lastPos = pos; lastVal = val; repaint(); }
}

void LfoView::mouseDown (const juce::MouseEvent& e)
{
    auto& sh = proc.shape;
    const auto p = e.position;
    const auto gr = graph();
    drag = Drag::none;
    if (strip().expanded (0, 4).contains (p))
    {
        const float x = (p.x - gr.getX()) / gr.getWidth();
        drag = std::abs (x - sh.loopStart) < std::abs (x - sh.loopEnd) ? Drag::loopStart : Drag::loopEnd;
        mouseDrag (e);
        return;
    }
    const int hp = hitPoint (p);
    if (hp >= 0)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            if (sh.pts.size() > 1) { sh.pts.erase (sh.pts.begin() + hp); changed(); }
            return;
        }
        drag = Drag::point; dragIndex = hp;
        return;
    }
    const int hc = hitCurve (p);
    if (hc >= 0) { drag = Drag::curve; dragIndex = hc; return; }
    if (! gr.expanded (4).contains (p) || e.mods.isPopupMenu()) return;

    wb::ShapePoint np;
    np.x = snapX ((p.x - gr.getX()) / gr.getWidth(), e.mods.isShiftDown());
    np.y = juce::jlimit (0.0f, 1.0f, (gr.getBottom() - p.y) / gr.getHeight());
    for (auto& q : sh.pts) if (std::abs (q.x - np.x) < 1e-4f) { q.y = np.y; changed(); return; }
    sh.pts.push_back (np);
    sh.sort();
    for (int i = 0; i < (int) sh.pts.size(); ++i) if (std::abs (sh.pts[(size_t) i].x - np.x) < 1e-6f) dragIndex = i;
    drag = Drag::point;
    changed();
}

void LfoView::mouseDrag (const juce::MouseEvent& e)
{
    auto& sh = proc.shape;
    const auto gr = graph();
    const float fx = (e.position.x - gr.getX()) / gr.getWidth();
    const float fy = juce::jlimit (0.0f, 1.0f, (gr.getBottom() - e.position.y) / gr.getHeight());
    const float step = 1.0f / (float) sh.grid;
    switch (drag)
    {
        case Drag::point:
        {
            if (dragIndex < 0 || dragIndex >= (int) sh.pts.size()) return;
            const int n = (int) sh.pts.size();
            float x = snapX (fx, e.mods.isShiftDown());
            const float lo = dragIndex > 0 ? sh.pts[(size_t) dragIndex - 1].x + 0.002f : 0.0f;
            const float hi = dragIndex < n - 1 ? sh.pts[(size_t) dragIndex + 1].x - 0.002f : 0.999f;
            sh.pts[(size_t) dragIndex].x = juce::jlimit (lo, juce::jmax (lo, hi), x);
            sh.pts[(size_t) dragIndex].y = fy;
            break;
        }
        case Drag::curve:
        {
            if (dragIndex < 0 || dragIndex >= (int) sh.pts.size()) return;
            const float dy = (float) e.getDistanceFromDragStartY();
            static float startCurve = 0;
            if (e.mouseWasDraggedSinceMouseDown() == false) startCurve = sh.pts[(size_t) dragIndex].curve;
            auto& pt = sh.pts[(size_t) dragIndex];
            const size_t ni = (size_t) (dragIndex + 1) % sh.pts.size();
            const float dir = sh.pts[ni].y >= pt.y ? 1.0f : -1.0f;
            pt.curve = juce::jlimit (-1.0f, 1.0f, startCurve + dir * dy / 90.0f);
            break;
        }
        case Drag::loopStart:
            sh.loopStart = juce::jlimit (0.0f, sh.loopEnd - step, std::round (fx * sh.grid) / (float) sh.grid);
            break;
        case Drag::loopEnd:
            sh.loopEnd = juce::jlimit (sh.loopStart + step, 1.0f, std::round (fx * sh.grid) / (float) sh.grid);
            break;
        default: return;
    }
    changed();
}

void LfoView::mouseUp (const juce::MouseEvent&) { drag = Drag::none; dragIndex = -1; }

void LfoView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hp = hitPoint (e.position);
    if (hp < 0) return;
    auto& pt = proc.shape.pts[(size_t) hp];
    pt.seg = pt.seg == wb::Seg::curve ? wb::Seg::smooth : (pt.seg == wb::Seg::smooth ? wb::Seg::hold : wb::Seg::curve);
    changed();
}

// ====================================================================== XY pad
static const char* xNeg[] = { "SLOWER", "ROUNDER", "PUSH", "DIVE" };
static const char* xPos[] = { "FASTER", "SHARPER", "SHUFFLE", "RISE" };
static const char* yNeg[] = { "SOFTER", "DRIFT", "DARKER", "CLEANER" };
static const char* yPos[] = { "DEEPER", "GLITCH", "BRIGHTER", "DIRTIER" };

XYPad::XYPad (WobbleProcessor& p) : proc (p)
{
    px = p.state.getParameter (id::xyX);
    py = p.state.getParameter (id::xyY);
    setTooltip ("XY pad: the centre is your patch. Drag to push the wobble; the buttons pick what each axis does. Double-click to centre. Right-click: automate X or Y.");
}

void XYPad::paint (juce::Graphics& g)
{
    const auto r = pad();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 8.0f);
    for (int k = 1; k < 4; ++k)
    {
        g.setColour (k == 2 ? col::mag.withAlpha (0.35f) : col::line);
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * k / 4.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * k / 4.0f), r.getX(), r.getRight());
    }
    g.setColour (col::mag.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 8.0f, 1.2f);

    const int xt = (int) proc.state.getRawParameterValue (id::xTarget)->load();
    const int yt = (int) proc.state.getRawParameterValue (id::yTarget)->load();
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (col::dim);
    auto lr = r.reduced (8.0f);
    g.drawText (yPos[yt], lr.removeFromTop (14), juce::Justification::centred);
    lr = r.reduced (8.0f);
    g.drawText (yNeg[yt], lr.removeFromBottom (14), juce::Justification::centred);
    {
        juce::Graphics::ScopedSaveState s (g);
        const auto c = r.getCentre();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
        g.drawText (xNeg[xt], juce::Rectangle<float> (c.x - 80, r.getX() - c.x + c.y + 4, 160, 14), juce::Justification::centred);
    }
    {
        juce::Graphics::ScopedSaveState s (g);
        const auto c = r.getCentre();
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, c.x, c.y));
        g.drawText (xPos[xt], juce::Rectangle<float> (c.x - 80, c.y - (r.getRight() - c.x) + 4, 160, 14), juce::Justification::centred);
    }

    const float vx = px->convertFrom0to1 (px->getValue()), vy = py->convertFrom0to1 (py->getValue());
    auto toPt = [&] (float x, float y) { return juce::Point<float> (r.getX() + (x + 1) * 0.5f * r.getWidth(), r.getBottom() - (y + 1) * 0.5f * r.getHeight()); };
    for (size_t i = 0; i < trail.size(); ++i)
    {
        const float a = (float) (i + 1) / (float) (trail.size() + 1);
        const auto t = toPt (trail[i].x, trail[i].y);
        g.setColour (col::mag.withAlpha (0.35f * a));
        g.fillEllipse (t.x - 4 * a, t.y - 4 * a, 8 * a, 8 * a);
    }
    const auto d = toPt (vx, vy);
    g.setColour (col::mag.withAlpha (0.25f)); g.fillEllipse (d.x - 14, d.y - 14, 28, 28);
    g.setColour (col::mag); g.fillEllipse (d.x - 7, d.y - 7, 14, 14);
    g.setColour (juce::Colours::white); g.fillEllipse (d.x - 2.5f, d.y - 2.5f, 5, 5);

    // readout
    const bool snap = proc.state.getRawParameterValue (id::xySnap)->load() > 0.5f;
    juce::String xs, ys;
    switch (xt)
    {
        case 0:
        {
            const double m = wb::xyRateMultiplier (vx, snap);
            xs = "RATE " + (snap ? wb::rateStepNames[juce::jlimit (0, 8, (int) std::lround ((vx + 1.0) * 4.0))]
                                 : (m >= 1 ? "x" + juce::String (m, 2) : "/" + juce::String (1.0 / m, 2)));
            break;
        }
        case 1: xs = "SHAPE " + juce::String (juce::roundToInt (vx * 100)) + "%"; break;
        case 2: xs = "SWING " + juce::String (juce::roundToInt (vx * 100)) + "%"; break;
        default: xs = "PITCH " + juce::String (vx * 12.0f, 1) + " st"; break;
    }
    switch (yt)
    {
        case 0: ys = "DEPTH x" + juce::String (1.0f + vy, 2); break;
        case 1: ys = "CHAOS " + juce::String (juce::roundToInt (vy * 100)) + "%"; break;
        case 2: ys = "FILTER " + juce::String (vy * 3.0f, 1) + " oct"; break;
        default: ys = "DRIVE " + juce::String (juce::roundToInt (vy * 50)) + "%"; break;
    }
    g.setColour (col::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (xs + "   " + ys, r.reduced (10, 20).removeFromBottom (16), juce::Justification::centred);
}

void XYPad::setFromMouse (juce::Point<float> p)
{
    const auto r = pad();
    float nx = juce::jlimit (-1.0f, 1.0f, (p.x - r.getX()) / r.getWidth() * 2.0f - 1.0f);
    float ny = juce::jlimit (-1.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight() * 2.0f - 1.0f);
    if (std::abs (nx) < 0.03f) nx = 0.0f;   // gentle magnet at the centre lines
    if (std::abs (ny) < 0.03f) ny = 0.0f;
    px->setValueNotifyingHost (px->convertTo0to1 (nx));
    py->setValueNotifyingHost (py->convertTo0to1 (ny));
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        rightClick = true;
        juce::PopupMenu m;
        bool any = false;
        if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
            if (auto* ctx = ed->getHostContext())
            {
                if (auto mx = ctx->getContextMenuForParameter (px)) { m.addSubMenu ("XY X  (left / right)", mx->getEquivalentPopupMenu()); any = true; }
                if (auto my = ctx->getContextMenuForParameter (py)) { m.addSubMenu ("XY Y  (up / down)", my->getEquivalentPopupMenu()); any = true; }
            }
        if (! any) m.addItem (1, "Centre");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                         [this] (int r) { if (r == 1) centre(); });
        return;
    }
    rightClick = false;
    if (springing) { px->endChangeGesture(); py->endChangeGesture(); springing = false; }
    dragging = true;
    px->beginChangeGesture(); py->beginChangeGesture();
    setFromMouse (e.position);
}
void XYPad::mouseDrag (const juce::MouseEvent& e) { if (dragging && ! rightClick) setFromMouse (e.position); }
void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (rightClick) { rightClick = false; return; }
    if (! dragging) return;
    dragging = false;
    if (proc.state.getRawParameterValue (id::xySpring)->load() > 0.5f) springing = true;   // keep the gesture open while it springs back
    else { px->endChangeGesture(); py->endChangeGesture(); }
}
void XYPad::mouseDoubleClick (const juce::MouseEvent& e) { if (! e.mods.isPopupMenu()) centre(); }

void XYPad::centre()
{
    if (springing) { px->endChangeGesture(); py->endChangeGesture(); springing = false; }
    px->beginChangeGesture(); py->beginChangeGesture();
    px->setValueNotifyingHost (px->convertTo0to1 (0.0f));
    py->setValueNotifyingHost (py->convertTo0to1 (0.0f));
    px->endChangeGesture(); py->endChangeGesture();
}

void XYPad::tick()
{
    float vx = px->convertFrom0to1 (px->getValue()), vy = py->convertFrom0to1 (py->getValue());
    if (springing)
    {
        vx *= 0.72f; vy *= 0.72f;
        if (std::abs (vx) < 0.01f && std::abs (vy) < 0.01f) { vx = vy = 0; }
        px->setValueNotifyingHost (px->convertTo0to1 (vx));
        py->setValueNotifyingHost (py->convertTo0to1 (vy));
        if (vx == 0 && vy == 0) { px->endChangeGesture(); py->endChangeGesture(); springing = false; }
    }
    const int xt = (int) proc.state.getRawParameterValue (id::xTarget)->load();
    const int yt = (int) proc.state.getRawParameterValue (id::yTarget)->load();
    const bool moved = vx != lastX || vy != lastY;
    if (moved) { trail.push_back ({ lastX, lastY }); if (trail.size() > 20) trail.pop_front(); }
    else if (! trail.empty()) trail.pop_front();
    if (moved || ! trail.empty() || xt != lastXT || yt != lastYT) repaint();
    lastX = vx; lastY = vy; lastXT = xt; lastYT = yt;
}

// ====================================================================== editor
void WobbleEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // header
    g.setColour (col::panel);
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (col::lime);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("WOBBLE-D", 16, 6, 200, 36, juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("dubstep wobble bass  v0.1.1", 152, 18, 200, 16, juce::Justification::left);
    for (auto& s : sections)
    {
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.second, s.first.getX(), s.first.getY(), 300, 14, juce::Justification::left);
        g.setColour (col::line);
        g.drawHorizontalLine (s.first.getY() + 15, (float) s.first.getX(), (float) s.first.getRight());
    }
}

WobbleEditor::WobbleEditor (WobbleProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    auto& st = proc.state;

    // header controls
    sourceChoice = make<Choice> (st, id::source, "", "Sound source: your sample, or a built-in tone (Sine, Saw, Square, Reese).");
    for (auto* b : { &loadBtn, &initBtn, &trimBtn, &normBtn }) content.addAndMakeVisible (b);
    loadBtn.setTooltip ("Load a sample (you can also drag a file from Explorer or FL's browser anywhere onto WOBBLE-D).");
    initBtn.setTooltip ("Reset everything to the starting patch (sine wobble, Reese tone, no sample).");
    trimBtn.setTooltip ("Auto-trim silence from the start/end of samples you load.");
    normBtn.setTooltip ("Auto-normalise samples you load to -1 dB peak.");
    loadBtn.onClick = [this] { openChooser(); };
    initBtn.onClick = [this] { proc.initPatch(); status = "Init patch"; };
    trimBtn.setClickingTogglesState (true); normBtn.setClickingTogglesState (true);
    trimBtn.setToggleState (proc.autoTrim, juce::dontSendNotification);
    normBtn.setToggleState (proc.autoNormalize, juce::dontSendNotification);
    trimBtn.onClick = [this] { proc.autoTrim = trimBtn.getToggleState(); };
    normBtn.onClick = [this] { proc.autoNormalize = normBtn.getToggleState(); };

    sampleView = make<SampleView> (proc);
    sampleView->onClickLoad = [this] { openChooser(); };

    rowSample = {
        make<Knob> (st, id::smpStart, "Start", "Where the sample starts playing."),
        make<Knob> (st, id::smpLength, "Length", "How much of the sample plays after Start."),
        make<Toggle> (st, id::smpLoop, "LOOP", "Loop", "Loop the Start..Length region while the note is held (Fade Out sets the loop crossfade)."),
        make<Knob> (st, id::smpFadeIn, "Fade In", "Fade at the start of each note."),
        make<Knob> (st, id::smpFadeOut, "Fade Out", "Fade at the end of the region (or the loop crossfade when Loop is on)."),
        make<Toggle> (st, id::smpReverse, "REV", "Reverse", "Play the region backwards."),
        make<Knob> (st, id::tune, "Tune", "Transpose in semitones (also moves the built-in tones)."),
        make<Knob> (st, id::fine, "Fine", "Fine tune in cents."),
        make<Knob> (st, id::rootKey, "Root", "The note the sample was recorded at. Detected automatically when you load; change it if it plays out of tune."),
    };

    // LFO
    lfoView = make<LfoView> (proc);
    lfoView->setTooltip ("Draw the wobble. Click: add point · drag: move (Shift = off-grid) · right-click: delete · double-click a point: Curve / S-smooth / Hold step · drag the small ring on a line: bend it · drag the top triangles: loop section.");
    for (auto* c : { (juce::Component*) &gridBox, (juce::Component*) &presetBox, (juce::Component*) &randBtn, (juce::Component*) &loopAllBtn })
        content.addAndMakeVisible (c);
    const std::pair<const char*, int> grids[] = { { "Grid 4", 4 }, { "Grid 8", 8 }, { "Grid 16", 16 }, { "Grid 32", 32 },
                                                  { "Grid 3 (T)", 3 }, { "Grid 6 (T)", 6 }, { "Grid 12 (T)", 12 }, { "Grid 24 (T)", 24 } };
    for (auto& gpair : grids) gridBox.addItem (gpair.first, gpair.second);
    gridBox.setSelectedId (proc.shape.grid, juce::dontSendNotification);
    gridBox.setTooltip ("Grid the points and loop markers lock to (steps per wobble cycle).");
    gridBox.onChange = [this] { proc.shape.grid = gridBox.getSelectedId(); proc.shapeChanged(); lfoView->repaint(); };
    presetBox.setTextWhenNothingSelected ("Shape presets");
    presetBox.addItemList (wb::Shape::presetNames(), 1);
    presetBox.onChange = [this]
    {
        const int i = presetBox.getSelectedItemIndex();
        if (i < 0) return;
        auto s = wb::Shape::preset (wb::Shape::presetNames()[i]);
        s.grid = proc.shape.grid;
        proc.shape = s;
        proc.shapeChanged();
        lfoView->repaint();
        presetBox.setSelectedId (0, juce::dontSendNotification);
    };
    randBtn.setTooltip ("New random pattern inside the loop section, locked to the grid.");
    randBtn.onClick = [this]
    {
        juce::Random r (juce::Time::currentTimeMillis());
        proc.shape.randomize (r);
        proc.shapeChanged();
        lfoView->repaint();
    };
    loopAllBtn.setTooltip ("Loop the whole shape again.");
    loopAllBtn.onClick = [this] { proc.shape.loopStart = 0; proc.shape.loopEnd = 1; proc.shapeChanged(); lfoView->repaint(); };

    content.addAndMakeVisible (rerollBtn);
    rerollBtn.setTooltip ("New chaos pattern. Chaos repeats exactly the same on every play and export until you reroll.");
    rerollBtn.onClick = [this] { proc.rerollChaos(); };
    rowWobble = {
        make<Choice> (st, id::rate, "Rate", "Wobble speed, locked to FL's tempo."),
        make<Choice> (st, id::lfoMode, "Mode", "Song-lock: locked to the playlist position (same every play). Note: restarts on each new note. Free: runs on its own in Hz."),
        make<Choice> (st, id::quantize, "Quantize", "Touch: rate changes are instant and slide back onto the grid. 1/16..Bar: rate changes land on the next grid line."),
        make<Knob> (st, id::freeRate, "Free Hz", "Speed in Hz when Mode is Free."),
        make<Knob> (st, id::resync, "Re-sync", "How fast the wobble slides back onto the beat grid after a rate change (in beats)."),
        make<Knob> (st, id::smooth, "Smooth", "Rounds off sharp corners in the shape."),
        make<Knob> (st, id::chaos, "Chaos", "+ hard random steps (glitch) / - smooth organic drift. Centre = none."),
        &rerollBtn,
        make<Knob> (st, id::velDepth, "Vel>Depth", "Harder notes wobble deeper."),
    };
    rowTargets = {
        make<Knob> (st, id::dCutoff, "Cutoff", "How far the wobble moves the filter (up to 6 octaves, - = closes)."),
        make<Knob> (st, id::dVolume, "Volume", "Wobble the volume (gate-style)."),
        make<Knob> (st, id::dPitch, "Pitch", "Wobble the pitch in semitones."),
        make<Knob> (st, id::dDrive, "Drive", "Wobble the drive amount."),
        make<Knob> (st, id::dBoost, "Boost", "Wobble the Boost sweep frequency (the 'yoy' vowel). Turn Boost dB up to hear it."),
    };
    rowVoice = {
        make<Choice> (st, id::voiceMode, "Voices", "Mono: one note, legato glide (classic bass). Poly: chords (up to 6 notes)."),
        make<Knob> (st, id::glide, "Glide", "Slide time between legato notes (Mono)."),
        make<Knob> (st, id::attack, "Attack", "Amp envelope attack."),
        make<Knob> (st, id::decay, "Decay", "Amp envelope decay."),
        make<Knob> (st, id::sustain, "Sustain", "Amp envelope sustain level."),
        make<Knob> (st, id::release, "Release", "Amp envelope release."),
        make<Knob> (st, id::unison, "Unison", "Stacked copies for a wider, thicker tone (the sub stays single)."),
        make<Knob> (st, id::detune, "Detune", "How far apart the unison copies are tuned."),
    };

    // XY
    xyPad = make<XYPad> (proc);
    for (int i = 0; i < 4; ++i)
    {
        auto& xb = xBtns[i]; auto& yb = yBtns[i];
        xb.setButtonText (wb::xTargetNames[i]); yb.setButtonText (wb::yTargetNames[i]);
        xb.param = proc.state.getParameter (id::xTarget); yb.param = proc.state.getParameter (id::yTarget);
        xb.getProperties().set ("accent", (juce::int64) col::mag.getARGB());
        yb.getProperties().set ("accent", (juce::int64) col::mag.getARGB());
        xb.setTooltip ("Make the pad's left/right control " + wb::xTargetNames[i]);
        yb.setTooltip ("Make the pad's up/down control " + wb::yTargetNames[i]);
        content.addAndMakeVisible (xb); content.addAndMakeVisible (yb);
        auto setChoice = [this] (const char* pid, int v)
        {
            if (auto* prm = proc.state.getParameter (pid))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
                prm->endChangeGesture();
            }
        };
        xb.onClick = [setChoice, i] { setChoice (id::xTarget, i); };
        yb.onClick = [setChoice, i] { setChoice (id::yTarget, i); };
    }
    content.addAndMakeVisible (centreBtn);
    centreBtn.setTooltip ("Snap the dot back to the centre (your patch as set).");
    centreBtn.onClick = [this] { xyPad->centre(); };
    springT = make<Toggle> (st, id::xySpring, "SPRING", "", "The dot glides back to centre when you let go.");
    snapT = make<Toggle> (st, id::xySnap, "SNAP", "", "RATE locks to musical steps (/2, x1.5, x2...). Off = smooth sweep.");
    glideK = make<Knob> (st, id::xyGlide, "Glide", "Smooths pad moves so fast drags never click.");
    springT->button.getProperties().set ("accent", (juce::int64) col::mag.getARGB());
    snapT->button.getProperties().set ("accent", (juce::int64) col::mag.getARGB());
    glideK->slider.getProperties().set ("accent", (juce::int64) col::mag.getARGB());

    rowFilter = {
        make<Choice> (st, id::fltType, "Filter", "Filter type. LP 24 is the classic wobble."),
        make<Knob> (st, id::cutoff, "Cutoff", "Filter cutoff (the wobble moves it from here)."),
        make<Knob> (st, id::reso, "Reso", "Filter resonance (the 'talk')."),
        make<Knob> (st, id::drive, "Drive", "Distortion before the filter."),
        make<Toggle> (st, id::hq, "HQ", "HQ", "Oversampled drive (cleaner top end). Off = Eco mode for weaker PCs."),
    };
    rowEq = {
        make<Knob> (st, id::lowCut, "Low Cut", "Removes lows from the wobble layer (the Sub stays)."),
        make<Knob> (st, id::lowGain, "Low", "Low shelf at 120 Hz."),
        make<Knob> (st, id::midGain, "Mid", "Mid bell."),
        make<Knob> (st, id::midFreq, "Mid Hz", "Mid bell frequency."),
        make<Knob> (st, id::highGain, "High", "High shelf at 6 kHz."),
        make<Knob> (st, id::highCut, "High Cut", "Removes harsh top end."),
        make<Knob> (st, id::boostFreq, "Boost Hz", "Boost sweep frequency (wobble it with the Boost target)."),
        make<Knob> (st, id::boostGain, "Boost dB", "How strong the boost peak is."),
        make<Knob> (st, id::boostQ, "Boost Q", "How narrow the boost peak is."),
    };
    rowOut = {
        make<Toggle> (st, id::subLock, "SUB", "Sub Lock", "Below Sub Hz everything is a clean mono sine; only the mids wobble."),
        make<Knob> (st, id::subFreq, "Sub Hz", "Where the clean sub takes over."),
        make<Knob> (st, id::subLevel, "Sub Lvl", "Sub level."),
        make<Choice> (st, id::subOct, "Sub Oct", "Sub at the note, or one octave down."),
        make<Choice> (st, id::subOut, "Sub Out", "Main: sub mixed in. Separate: sub goes to WOBBLE-D output 3-4 so you can route it to its own mixer insert."),
        make<Knob> (st, id::width, "Width", "Stereo width of the wobble layer."),
        make<Toggle> (st, id::mono, "MONO", "Mono", "Fold the wobble layer to mono."),
        make<Knob> (st, id::volume, "Volume", "Output level."),
        make<Toggle> (st, id::clip, "CLIP", "Clipper", "Soft clipper so the output never goes over 0 dB."),
    };
    for (auto* c : rowOut)
        if (auto* k = dynamic_cast<Knob*> (c); k != nullptr && (c == rowOut[1] || c == rowOut[2]))
            k->slider.getProperties().set ("accent", (juce::int64) col::sub.getARGB());

    keepFocusInHost();
    setResizable (true, true);
    setResizeLimits (800, 500, 1920, 1200);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / baseH);
    startTimerHz (30);
    setSize (1088, 680);   // last (see playbook)
}

WobbleEditor::~WobbleEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void WobbleEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

// Clicking WOBBLE-D must never take the keyboard away from FL (typing keyboard / shortcuts keep working).
static void noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
    for (auto* ch : c.getChildren()) noFocus (*ch);
}
void WobbleEditor::keepFocusInHost()
{
    noFocus (*this);
    if (auto* top = getTopLevelComponent())
    {
        top->setWantsKeyboardFocus (false);
        top->setMouseClickGrabsKeyboardFocus (false);   // window won't activate on click (WM_MOUSEACTIVATE -> NOACTIVATE)
    }
}
void WobbleEditor::parentHierarchyChanged() { keepFocusInHost(); }

void WobbleEditor::resized()
{
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
    layoutContent();
}

static void layRow (const std::vector<juce::Component*>& row, juce::Rectangle<int> r)
{
    if (row.empty()) return;
    // drop-downs get a wider cell so their text fits
    auto weight = [] (juce::Component* c) { return dynamic_cast<Choice*> (c) != nullptr ? 1.5f : 1.0f; };
    float total = 0;
    for (auto* c : row) total += weight (c);
    const float unit = (float) r.getWidth() / total;
    for (auto* c : row)
    {
        auto cell = r.removeFromLeft (juce::roundToInt (unit * weight (c)));
        if (auto* tb = dynamic_cast<juce::TextButton*> (c)) tb->setBounds (cell.withTrimmedBottom (14).withSizeKeepingCentre (juce::jmin (cell.getWidth() - 6, 84), 26));
        else c->setBounds (cell);
    }
}

void WobbleEditor::layoutContent()
{
    if (sampleView == nullptr) return;
    auto& S = content.sections;
    S.clear();
    const int L = 16, LW = 720, R = 752, RW = 512, rowH = 68;

    // header
    sourceChoice->setBounds (360, 8, 150, 38);
    sourceChoice->box.setBounds (0, 4, 150, 26);
    loadBtn.setBounds (522, 12, 110, 26);
    trimBtn.setBounds (640, 12, 56, 26);
    normBtn.setBounds (702, 12, 56, 26);
    initBtn.setBounds (1196, 12, 68, 26);

    int y = 54;
    S.push_back ({ { L, y, LW, 16 }, "SAMPLE" });
    sampleView->setBounds (L, y + 18, LW, 92);
    layRow (rowSample, { L, y + 114, LW, rowH });
    y += 114 + rowH + 4;                                     // 240
    S.push_back ({ { L, y, LW, 16 }, "WOBBLE SHAPE" });
    gridBox.setBounds (L + LW - 438, y - 4, 104, 22);
    presetBox.setBounds (L + LW - 328, y - 4, 120, 22);
    randBtn.setBounds (L + LW - 202, y - 4, 100, 22);
    loopAllBtn.setBounds (L + LW - 96, y - 4, 96, 22);
    lfoView->setBounds (L, y + 22, LW, 214);
    y += 22 + 214 + 6;                                       // 482
    S.push_back ({ { L, y, LW, 16 }, "WOBBLE" });
    layRow (rowWobble, { L, y + 18, LW, rowH });
    y += 18 + rowH + 6;                                      // 574
    S.push_back ({ { L, y, LW, 16 }, "WOBBLE TARGETS  (how much the wobble moves each one)" });
    layRow (rowTargets, { L, y + 18, LW * 5 / 9, rowH });
    y += 18 + rowH + 6;                                      // 666
    S.push_back ({ { L, y, LW, 16 }, "VOICE" });
    layRow (rowVoice, { L, y + 18, LW, rowH });

    // right column: XY
    int ry = 54;
    S.push_back ({ { R, ry, RW, 16 }, "XY PAD" });
    const int padX = R + 86, padY = ry + 22, padS = 330;
    for (int i = 0; i < 4; ++i) yBtns[i].setBounds (R, padY + 8 + i * 44, 78, 34);
    xyPad->setBounds (padX, padY, padS, padS);
    for (int i = 0; i < 4; ++i) xBtns[i].setBounds (padX + i * (padS / 4) + 2, padY + padS + 6, padS / 4 - 4, 28);
    const int cx = padX + padS + 10, cw = R + RW - cx;
    centreBtn.setBounds (cx, padY + 8, cw, 28);
    springT->setBounds (cx, padY + 44, cw, 40);
    snapT->setBounds (cx, padY + 90, cw, 40);
    glideK->setBounds (cx, padY + 140, cw, 70);
    ry = padY + padS + 42;                                   // 448
    S.push_back ({ { R, ry, RW, 16 }, "FILTER + DRIVE" });
    layRow (rowFilter, { R, ry + 18, RW, rowH });
    ry += 18 + rowH + 6;
    S.push_back ({ { R, ry, RW, 16 }, "EQ + BOOST SWEEP" });
    layRow (rowEq, { R, ry + 18, RW, rowH });
    ry += 18 + rowH + 6;
    S.push_back ({ { R, ry, RW, 16 }, "SUB + OUTPUT" });
    layRow (rowOut, { R, ry + 18, RW, rowH });
    content.repaint();
}

void WobbleEditor::syncTargetButtons()
{
    const int xt = (int) proc.state.getRawParameterValue (id::xTarget)->load();
    const int yt = (int) proc.state.getRawParameterValue (id::yTarget)->load();
    for (int i = 0; i < 4; ++i)
    {
        if (xBtns[i].getToggleState() != (i == xt)) xBtns[i].setToggleState (i == xt, juce::dontSendNotification);
        if (yBtns[i].getToggleState() != (i == yt)) yBtns[i].setToggleState (i == yt, juce::dontSendNotification);
    }
}

void WobbleEditor::timerCallback()
{
    lfoView->tick();
    xyPad->tick();
    sampleView->refresh();
    syncTargetButtons();
    const auto seedTxt = "REROLL #" + juce::String ((int) proc.state.getRawParameterValue (id::seed)->load());
    if (rerollBtn.getButtonText() != seedTxt) rerollBtn.setButtonText (seedTxt);
    if (gridBox.getSelectedId() != proc.shape.grid) gridBox.setSelectedId (proc.shape.grid, juce::dontSendNotification);
}

bool WobbleEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void WobbleEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) { loadFile (juce::File (f)); return; }
}

void WobbleEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into WOBBLE-D", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFile (f);
                          });
}

void WobbleEditor::loadFile (const juce::File& f)
{
    juce::String err;
    if (! proc.loadSample (f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "WOBBLE-D", err);
    sampleView->refresh();
}
