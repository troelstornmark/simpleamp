#include "PluginEditor.h"

namespace
{
using sa_ui::pal;
using sa_ui::isSketch;
namespace sk = sa_ui::sketch;

const char* kAmpIds[]= { "input", "gate", "bass", "mid", "treble", "output" };
const char* kAmpNames[] = { "INPUT", "GATE", "BASS", "MID", "TREBLE", "OUTPUT" };

constexpr int kWidth = 940, kHeight = 616;

void setupKnob (sa_ui::Knob& k, juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                const juce::String& labelText, juce::Slider::SliderStyle style, bool textBox)
{
    k.slider.setSliderStyle (style);
    k.slider.setTextBoxStyle (textBox ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, false, 64, 16);
    k.slider.setPopupDisplayEnabled (! textBox, true, nullptr, 1500);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, k.slider);
    if (auto* p = apvts.getParameter (id))
    {
        k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        k.slider.setTooltip (p->getName (64) + " (double-click to reset)");
    }
    parent.addAndMakeVisible (k.slider);
    k.label.setText (labelText, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred); // colour: Label::textColourId of the theme
    k.label.setFont (juce::FontOptions (textBox ? 17.0f : 14.0f, juce::Font::bold));
    k.label.setMinimumHorizontalScale (0.5f); // narrow pedal columns: squeeze rather than cut to "DE..."
    k.label.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (k.label);
}
} // namespace

namespace sa_ui
{
//==============================================================================
void Lnf::applyTheme()
{
    const auto& p = pal();
    if (isSketch())
    {
        setColourScheme (getLightColourScheme());
        for (auto id : { (int) juce::ToggleButton::textColourId, (int) juce::TextButton::textColourOffId, (int) juce::TextButton::textColourOnId,
                         (int) juce::ComboBox::textColourId, (int) juce::ComboBox::arrowColourId, (int) juce::PopupMenu::textColourId,
                         (int) juce::PopupMenu::highlightedTextColourId, (int) juce::PopupMenu::headerTextColourId,
                         (int) juce::Slider::textBoxTextColourId, (int) juce::TooltipWindow::textColourId, (int) juce::AlertWindow::textColourId,
                         (int) juce::TextEditor::textColourId, (int) juce::BubbleComponent::outlineColourId })
            setColour (id, p.text);
        setColour (juce::PopupMenu::backgroundColourId, p.bg);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent.withAlpha (0.22f));
        setColour (juce::ComboBox::backgroundColourId, p.field);
        setColour (juce::ComboBox::outlineColourId, p.fieldLine);
        setColour (juce::TextButton::buttonColourId, p.field);
        setColour (juce::AlertWindow::backgroundColourId, p.bg);
        setColour (juce::AlertWindow::outlineColourId, p.line);
        setColour (juce::TooltipWindow::backgroundColourId, p.bg);
        setColour (juce::TooltipWindow::outlineColourId, p.line);
        setColour (juce::BubbleComponent::backgroundColourId, p.field);
        setColour (juce::TextEditor::backgroundColourId, p.field);
        setColour (juce::TextEditor::outlineColourId, p.line);
        setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
        setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.25f));
        setColour (juce::ResizableWindow::backgroundColourId, p.bg);
    }
    else
    {
        setColourScheme (getMidnightColourScheme());
        setColour (juce::ComboBox::backgroundColourId, p.field);
        setColour (juce::ComboBox::outlineColourId, p.fieldLine);
        setColour (juce::PopupMenu::backgroundColourId, p.panel);
        setColour (juce::TooltipWindow::backgroundColourId, p.panel);
        setColour (juce::TextButton::buttonColourId, p.panel);
    }
    setColour (juce::Label::textColourId, p.dim);
    setColour (juce::ToggleButton::textColourId, p.text);
    setColour (juce::Slider::rotarySliderFillColourId, p.accent);
    setColour (juce::Slider::thumbColourId, p.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::tickColourId, p.accent);
    setColour (juce::TextButton::buttonOnColourId, p.accent);
}

void Lnf::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const bool small = std::min (w, h) < 60;
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (small ? 2.0f : 10.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const float angle = start + pos * (end - start);
    const float lineW = std::min (8.0f, radius * (small ? 0.32f : 0.5f));
    const float arcRadius = radius - lineW * 0.5f;
    const auto c = bounds.getCentre();

    if (isSketch())
    {
        // A pencil knob: circle with a pointer line, the setting shaded around it in the knob's colour.
        const auto seed = sk::seedOf (slider.getBounds().toFloat(), (juce::uint32) w);
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, start, end, true);
        sk::outline (g, track, seed, 0.9f, pal().dim);
        if (slider.isEnabled() && pos > 0.002f)
        {
            juce::Path value, band;
            value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, start, angle, true);
            juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createStrokedPath (band, value);
            sk::hatch (g, band, slider.findColour (juce::Slider::rotarySliderFillColourId), seed, 1.9f, 1.7f, true);
        }
        const float knobR = arcRadius - lineW * 0.5f - (small ? 1.5f : 4.0f);
        if (knobR > 3.0f)
        {
            juce::Path knob;
            knob.addEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c));
            sk::outline (g, knob, seed + 1u, small ? 1.1f : 1.4f);
            const juce::Point<float> dir (std::cos (angle - juce::MathConstants<float>::halfPi), std::sin (angle - juce::MathConstants<float>::halfPi));
            juce::Path pointer;
            pointer.startNewSubPath (c + dir * (knobR * 0.25f));
            pointer.lineTo (c + dir * (knobR * 0.95f));
            sk::outline (g, pointer, seed + 2u, small ? 1.5f : 2.0f);
        }
        return;
    }

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, start, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float thumb = lineW * 2.0f;
    const juce::Point<float> p (c.x + arcRadius * std::cos (angle - juce::MathConstants<float>::halfPi),
                                c.y + arcRadius * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre (p));
}

void Lnf::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                            juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    if (! isSketch() || ! (vertical || style == juce::Slider::LinearHorizontal))
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, slider);
        return;
    }
    const auto seed = sk::seedOf (slider.getBounds().toFloat(), 5u);
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float thick = 5.0f;
    const auto groove = vertical ? juce::Rectangle<float> (area.getCentreX() - thick * 0.5f, area.getY(), thick, area.getHeight())
                                 : juce::Rectangle<float> (area.getX(), area.getCentreY() - thick * 0.5f, area.getWidth(), thick);
    if (slider.isEnabled())
    {
        auto filled = vertical ? groove.withTop (pos) : groove.withRight (pos);
        sk::hatchBox (g, filled, 2.0f, slider.findColour (juce::Slider::trackColourId), seed, 1.8f, true);
    }
    sk::box (g, groove, 2.5f, seed, 0.9f, slider.isEnabled() ? pal().line : pal().dim);

    const float d = vertical ? std::min (13.0f, (float) w * 0.8f) : std::min (13.0f, (float) h * 0.8f);
    const auto knob = juce::Rectangle<float> (d, d).withCentre (vertical ? juce::Point<float> (area.getCentreX(), pos)
                                                                         : juce::Point<float> (pos, area.getCentreY()));
    juce::Path thumb;
    thumb.addEllipse (knob);
    g.setColour (pal().field);
    g.fillPath (thumb);
    sk::outline (g, thumb, seed + 9u, 1.3f, slider.isEnabled() ? pal().line : pal().dim);
}

void Lnf::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    if (! isSketch()) { LookAndFeel_V4::drawButtonBackground (g, b, bg, over, down); return; }
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const auto seed = sk::seedOf (b.getBounds().toFloat(), 11u);
    if (b.getToggleState() || down)
        sk::hatchBox (g, r, 5.0f, b.findColour (juce::TextButton::buttonOnColourId), seed, 1.5f);
    else if (over)
        sk::hatchBox (g, r, 5.0f, pal().text, seed, 0.3f);
    sk::box (g, r, 5.0f, seed, 1.2f, b.isEnabled() ? pal().line : pal().dim);
}

void Lnf::drawComboBox (juce::Graphics& g, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox& box)
{
    if (! isSketch()) { LookAndFeel_V4::drawComboBox (g, w, h, down, bx, by, bw, bh, box); return; }
    const auto seed = sk::seedOf (box.getBounds().toFloat(), 23u);
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (1.5f);
    if (down) sk::hatchBox (g, r, 4.0f, pal().text, seed, 0.25f);
    sk::box (g, r, 4.0f, seed, 1.2f, box.isEnabled() ? pal().line : pal().dim);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.5f, ay - 2.5f);
    arrow.lineTo (ax, ay + 3.0f);
    arrow.lineTo (ax + 4.5f, ay - 2.5f);
    sk::outline (g, arrow, seed + 4u, 1.4f, box.isEnabled() ? pal().line : pal().dim);
}

void Lnf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    if (! isSketch()) { LookAndFeel_V4::drawToggleButton (g, b, over, down); return; }
    const auto seed = sk::seedOf (b.getBounds().toFloat(), 31u);
    const auto f = themed (juce::Font (juce::FontOptions (std::min (15.0f, (float) b.getHeight() * 0.75f))));
    const float size = std::min (16.0f, (float) b.getHeight() - 4.0f);
    const auto tick = juce::Rectangle<float> (4.0f, ((float) b.getHeight() - size) * 0.5f, size, size);
    const auto line = b.isEnabled() ? pal().line : pal().dim;
    if (over) sk::hatchBox (g, tick, 3.0f, pal().text, seed, 0.25f);
    sk::box (g, tick, 3.0f, seed, 1.2f, line);
    if (b.getToggleState())
    {
        juce::Path v;
        v.startNewSubPath (tick.getX() + size * 0.2f, tick.getY() + size * 0.55f);
        v.lineTo (tick.getX() + size * 0.45f, tick.getY() + size * 0.82f);
        v.lineTo (tick.getX() + size * 1.05f, tick.getY() - size * 0.1f);
        sk::outline (g, v, seed + 3u, 2.4f, b.isEnabled() ? pal().accent : pal().dim);
    }
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (f);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (juce::roundToInt (size) + 10).withTrimmedRight (2),
                      juce::Justification::centredLeft, 1);
}

void Lnf::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    if (! isSketch()) { LookAndFeel_V4::drawPopupMenuBackground (g, w, h); return; }
    const auto r = juce::Rectangle<int> (w, h).toFloat();
    sk::paper (g, r);
    sk::box (g, r.reduced (1.5f), 3.0f, (juce::uint32) (w * 31 + h), 1.1f);
}

void Lnf::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    if (! isSketch()) { LookAndFeel_V4::drawTooltip (g, text, w, h); return; }
    const auto r = juce::Rectangle<int> (w, h).toFloat();
    sk::paper (g, r);
    sk::box (g, r.reduced (1.5f), 3.0f, (juce::uint32) (w * 31 + h), 1.0f);
    g.setColour (pal().text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 2), juce::Justification::centredLeft, 4);
}

//==============================================================================
void Led::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float d = std::min (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    if (isSketch())
    {
        juce::Path circle;
        circle.addEllipse (r.reduced (1.0f));
        const auto seed = sk::seedOf (getBounds().toFloat(), (juce::uint32) colour.getARGB());
        if (getToggleState()) sk::hatch (g, circle, colour, seed, 2.2f, 1.6f, true);
        else if (over) sk::hatch (g, circle, pal().text, seed, 0.3f);
        sk::outline (g, circle, seed, 1.2f);
        return;
    }
    if (getToggleState())
    {
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (r.expanded (2.0f));
        g.setColour (colour.brighter (0.3f));
    }
    else
    {
        g.setColour (juce::Colour (over ? 0xff4a4a50 : 0xff38383d));
    }
    g.fillEllipse (r);
}

//==============================================================================
PedalTile::PedalTile (APVTS& state, const juce::String& n, const juce::String& onParamId, juce::Colour c)
    : apvts (state), name (n), colour (c), led (c)
{
    addAndMakeVisible (led);
    led.setTooltip ("On / off (or click the pedal name)");
    ledAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, onParamId, led);
}

void PedalTile::addKnob (const juce::String& paramId, const juce::String& label, bool isVertical)
{
    vertical = isVertical;
    auto k = std::make_unique<Knob>();
    setupKnob (*k, *this, apvts, paramId, label,
               isVertical ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag, false);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->slider.setColour (juce::Slider::trackColourId, colour);
    knobs.push_back (std::move (k));
}

void PedalTile::refresh()
{
    if (led.getToggleState() != lastOn) { lastOn = led.getToggleState(); repaint(); }
}

void PedalTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto& p = pal();
    if (isSketch())
    {
        // Pencil drawing: shaded in the pedal's colour when on, just a hint of it when off.
        const auto seed = (juce::uint32) name.hashCode();
        const auto body = r.reduced (2.0f);
        sk::hatchBox (g, body, 7.0f, colour, seed, lastOn ? 0.55f : 0.1f);
        sk::box (g, body, 7.0f, seed, lastOn ? 1.6f : 1.2f);
        if (lastOn) sk::box (g, body.reduced (1.5f), 6.0f, seed + 5u, 1.0f, colour);
        sk::text (g, name, { 9.0f, 3.0f, (float) getWidth() - 36.0f, 24.0f }, juce::Justification::centredLeft,
                  sa_ui::font (16.0f, true), lastOn ? p.text : p.dim);
        return;
    }
    g.setColour (lastOn ? colour.withAlpha (0.16f).overlaidWith (p.panel.withAlpha (0.55f)) : p.panel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (lastOn ? colour.withAlpha (0.8f) : p.line);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.2f);
    g.setColour (lastOn ? p.text : p.dim);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawFittedText (name, 9, 3, getWidth() - 34, 24, juce::Justification::centredLeft, 1, 0.6f); // squeeze, don't cut
}

void PedalTile::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto header = r.removeFromTop (22);
    led.setBounds (header.removeFromRight (20));
    if (extra != nullptr) extra->setBounds (r.removeFromBottom (24).reduced (0, 1));
    if (knobs.empty()) return;

    const int w = r.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto col = r.removeFromLeft (w);
        k->label.setBounds (col.removeFromBottom (18));
        k->slider.setBounds (vertical ? col.reduced (w > 30 ? (w - 24) / 2 : 1, 0) : col.reduced (1));
    }
}

void PedalTile::mouseUp (const juce::MouseEvent& e)
{
    if (e.y < 28) led.triggerClick(); // click the name to toggle, like stepping on it
}

//==============================================================================
FilePicker::FilePicker (juce::String k, juce::String none, juce::String pat)
    : kind (std::move (k)), noneText (std::move (none)), pattern (std::move (pat))
{
    onChange = [this] { picked(); };
    setTooltip (kind == "amps" ? juce::String ("Your amp captures (tones folder and ~/Music/SimpleAmp/amps), or Browse...")
                               : "Files from the " + kind + " folder (~/Music/SimpleAmp/" + kind + "), or Browse...");
}

void FilePicker::refresh (bool rescan)
{
    const auto current = getCurrent ? getCurrent() : juce::File();
    const bool amps = kind == "amps";
    auto found = ! rescan && getNumItems() > 0 ? files
                 : amps ? SimpleAmpProcessor::findAmpFiles() : SimpleAmpProcessor::findLibraryFiles (kind);
    if (found == files && current == shownCurrent && getNumItems() > 0) return;

    files = found;
    shownCurrent = current;
    clear (juce::dontSendNotification);
    addItem (noneText, 1);
    juce::String lastGroup;
    for (int i = 0; i < files.size(); ++i)
    {
        if (amps)
        {
            const auto group = SimpleAmpProcessor::ampGroup (files[i]);
            if (group != lastGroup) { addSectionHeading (group); lastGroup = group; }
        }
        addItem (amps ? SimpleAmpProcessor::displayName (files[i]) : files[i].getFileNameWithoutExtension(), i + 2);
    }
    if (current != juce::File() && ! files.contains (current))
        addItem (current.getFileNameWithoutExtension(), 9998);
    addSeparator();
    addItem ("Browse...", 9999);

    const int idx = files.indexOf (current);
    setSelectedId (current == juce::File() ? 1 : idx >= 0 ? idx + 2 : 9998, juce::dontSendNotification);
}

void FilePicker::picked()
{
    const int id = getSelectedId();
    if (id == 9999)
    {
        auto start = SimpleAmpProcessor::defaultFolder().getChildFile (kind);
        if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Choose a file", start, pattern);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile() && onPick) onPick (f);
                                  files.clear(); // force the list to rebuild and show the pick
                                  refresh();
                              });
        return;
    }
    if (id == 9998 || id == 0) return;
    const auto f = id == 1 ? juce::File() : files[id - 2];
    if (onPick) onPick (f);
}

//==============================================================================
TunerPanel::TunerPanel (SimpleAmpProcessor& p) : proc (p)
{
    muteButton.setToggleState (proc.isTunerMuted(), juce::dontSendNotification);
    muteButton.onClick = [this] { proc.setTunerMute (muteButton.getToggleState()); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (muteButton);
    addAndMakeVisible (closeButton);
    setInterceptsMouseClicks (true, true); // the board underneath can't be touched while tuning
    setOpaque (true);
}

TunerPanel::~TunerPanel() { proc.setTunerActive (false); }

void TunerPanel::visibilityChanged()
{
    proc.setTunerActive (isVisible());
    hasPitch = false;
    if (isVisible()) startTimerHz (25); else stopTimer();
}

void TunerPanel::timerCallback()
{
    const double sr = proc.getHostRate() > 0 ? proc.getHostRate() : 48000.0;
    const int n = proc.copyTunerInput (samples.data(), (int) samples.size());
    const float hz = detector.detect (samples.data(), n, sr);
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (hz > 0.0f)
    {
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        if (! hasPitch || std::abs (midi - shownMidi) > 0.5f) shownMidi = midi;  // new note: jump
        else shownMidi += 0.35f * (midi - shownMidi);                            // same note: steady the needle
        shownHz = 440.0f * std::pow (2.0f, (shownMidi - 69.0f) / 12.0f);
        hasPitch = true;
        lastPitchTime = now;
    }
    else if (hasPitch && now - lastPitchTime > 1500.0)
    {
        hasPitch = false;
    }
    repaint();
}

void TunerPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto& p = pal();
    const bool sketch = isSketch();
    if (sketch)
    {
        sk::paper (g, r, sk::originIn (*this)); // fully covers the board underneath
        sk::box (g, r.reduced (2.5f), 10.0f, 404u, 1.6f, p.accent);
    }
    else
    {
        g.fillAll (p.bg); // fully covers the board underneath
        g.setColour (juce::Colour (0xff141417));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (p.accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.5f);
    }

    g.setColour (p.dim);
    g.setFont (sa_ui::font (17.0f, true));
    g.drawText ("TUNER", 18, 10, 200, 24, juce::Justification::centredLeft);
    g.setFont (sa_ui::font (13.0f, true));
    g.drawText ("A4 = 440 Hz  |  press T or Done to close", getWidth() - 318, 12, 300, 20, juce::Justification::centredRight);

    const bool fresh = hasPitch && juce::Time::getMillisecondCounterHiRes() - lastPitchTime < 250.0;
    const auto note = sa::PitchDetector::toNote (shownHz);
    const float cents = hasPitch ? (shownMidi - std::round (shownMidi)) * 100.0f : 0.0f;
    const juce::Colour colour = std::abs (cents) < 3.0f ? juce::Colour (0xff4caf50)
                              : std::abs (cents) < 15.0f ? juce::Colour (0xfff2b134) : juce::Colour (0xffe0572a);

    // Note name
    auto area = getLocalBounds().reduced (30, 40);
    auto noteArea = area.removeFromTop (area.getHeight() / 2);
    if (hasPitch)
    {
        const auto c = (fresh ? colour : p.dim).withAlpha (fresh ? 1.0f : 0.6f);
        const auto octaveArea = noteArea.withTrimmedLeft (noteArea.getWidth() / 2 + 60).withTrimmedTop (60).toFloat();
        if (sketch)
        {
            sk::text (g, note.name, noteArea.toFloat(), juce::Justification::centred, sa_ui::font (110.0f, true), c);
            sk::text (g, juce::String (note.octave), octaveArea, juce::Justification::centredLeft, sa_ui::font (26.0f, true), c);
        }
        else
        {
            g.setColour (c);
            g.setFont (juce::FontOptions (110.0f, juce::Font::bold));
            g.drawText (juce::String (note.name), noteArea, juce::Justification::centred);
            g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
            g.drawText (juce::String (note.octave), octaveArea, juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (p.dim);
        g.setFont (sa_ui::font (30.0f));
        g.drawText ("Play a single string", noteArea, juce::Justification::centred);
    }

    // Cents scale: -50 ... +50
    auto scale = area.removeFromTop (70).withSizeKeepingCentre (std::min (area.getWidth(), 640), 70).toFloat();
    const float cx = scale.getCentreX(), y = scale.getY() + 30.0f, half = scale.getWidth() / 2.0f;
    const juce::Rectangle<float> zone (cx - half * 0.06f, y - 20.0f, half * 0.12f, 40.0f);
    if (sketch) sk::hatchBox (g, zone, 4.0f, juce::Colour (0xff4caf50), 77u, 0.7f);
    else { g.setColour (juce::Colour (0xff4caf50).withAlpha (0.18f)); g.fillRoundedRectangle (zone, 4.0f); }
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = cx + half * c / 50.0f;
        const float h = c == 0 ? 22.0f : 12.0f;
        if (sketch)
        {
            juce::Path tick;
            tick.startNewSubPath (x, y - h);
            tick.lineTo (x, y + h);
            sk::outline (g, tick, (juce::uint32) (c + 100), c == 0 ? 2.0f : 1.4f, c == 0 ? p.text : p.dim);
            continue;
        }
        g.setColour (c == 0 ? p.text : p.dim);
        g.fillRect (x - 1.0f, y - h, 2.0f, h * 2.0f);
    }
    g.setColour (p.dim);
    g.setFont (sa_ui::font (12.0f));
    g.drawText ("-50", (int) (cx - half - 20), (int) (y + 24), 40, 16, juce::Justification::centred);
    g.drawText ("+50", (int) (cx + half - 20), (int) (y + 24), 40, 16, juce::Justification::centred);
    if (hasPitch)
    {
        const float x = cx + half * juce::jlimit (-50.0f, 50.0f, cents) / 50.0f;
        const juce::Rectangle<float> needle (x - 4.0f, y - 30.0f, 8.0f, 60.0f);
        if (sketch)
        {
            sk::hatchBox (g, needle, 4.0f, fresh ? colour : p.dim, 99u, 2.2f, true);
            sk::box (g, needle, 4.0f, 98u, 1.2f);
        }
        else
        {
            g.setColour (fresh ? colour : p.dim);
            g.fillRoundedRectangle (needle, 4.0f);
        }

        g.setColour (fresh ? colour : p.dim);
        g.setFont (sa_ui::font (18.0f));
        g.drawText ((cents >= 0 ? "+" : "") + juce::String (cents, 1) + " cents   |   " + juce::String (shownHz, 1) + " Hz",
                    area.removeFromTop (40), juce::Justification::centred);
    }
}

void TunerPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    auto bottom = r.removeFromBottom (32);
    closeButton.setBounds (bottom.removeFromRight (110));
    muteButton.setBounds (bottom.removeFromLeft (260));
}
} // namespace sa_ui

//==============================================================================
SimpleAmpEditor::SimpleAmpEditor (SimpleAmpProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    using namespace sa_ui;
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    // ---- Amp ----
    for (size_t i = 0; i < ampKnobs.size(); ++i)
        setupKnob (ampKnobs[i], *this, s, kAmpIds[i], kAmpNames[i], juce::Slider::RotaryHorizontalVerticalDrag, true);
    ampKnobs[1].slider.textFromValueFunction = [] (double v) { return v <= -95.0 ? juce::String ("off") : juce::String (v, 0) + " dB"; };
    ampKnobs[1].slider.updateText();

    for (auto* b : { &cabButton, &ecoButton }) addAndMakeVisible (*b);
    cabButton.setTooltip ("Turn off when the amp model is a full rig (amp + cab) capture");
    ecoButton.setTooltip ("Use the smaller network inside slimmable (A2) models to save CPU");
    cabAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "cab", cabButton);
    ecoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "eco", ecoButton);

    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 101 && id - 101 < userPresets.size()) proc.loadUserPreset (userPresets[id - 101]);
        else if (id >= 1 && id <= proc.getNumPrograms()) proc.setCurrentProgram (id - 1);
    };
    presetBox.setTooltip ("My presets: your whole setup (amp, pedals, files). Factory: amp starting points only.");
    presetBox.setTextWhenNothingSelected ("Presets");
    addAndMakeVisible (presetBox);
    refreshPresetList();

    saveButton.setTooltip ("Save everything (amp, cab, pedals, knobs) as a preset");
    saveButton.onClick = [this] { savePreset(); };
    deleteButton.setTooltip ("Delete the selected preset (it goes to the Trash)");
    deleteButton.onClick = [this] { deletePreset(); };
    tunerButton.setTooltip ("Tuner (T in SimpleAmp Live)");
    tunerButton.onClick = [this] { toggleTuner(); };
    for (auto* b : { &saveButton, &deleteButton, &tunerButton }) addAndMakeVisible (*b);

    // Look: dark, or pencil sketch on paper. Shared by every SimpleAmp window and remembered.
    for (auto* b : { &darkButton, &sketchButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setTooltip ("Look: dark, or pencil sketch on paper");
        addAndMakeVisible (*b);
    }
    darkButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    sketchButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    darkButton.onClick = [this] { if (darkButton.getToggleState()) { sa_ui::setSketch (false); applyTheme(); } };
    sketchButton.onClick = [this] { if (sketchButton.getToggleState()) { sa_ui::setSketch (true); applyTheme(); } };

    tuner = std::make_unique<TunerPanel> (proc);
    tuner->onClose = [this] { toggleTuner(); };
    addChildComponent (*tuner);

    inputChBox.addItemList ({ "In 1", "In 2" }, 1);
    inputChAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, "inputch", inputChBox);
    addAndMakeVisible (inputChBox);

    for (auto* l : { &ampLabel, &cabLabel, &statusLabel })
    {
        l->setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (*l);
    }
    ampLabel.setText ("Amp:", juce::dontSendNotification);
    cabLabel.setText ("Cab:", juce::dontSendNotification);

    ampPicker.getCurrent = [this] { return proc.getModelFile(); };
    ampPicker.onPick = [this] (const juce::File& f) { proc.selectAmp (f); };
    addAndMakeVisible (ampPicker);

    cabPicker.getCurrent = [this] { return proc.getCabFile(); };
    cabPicker.onPick = [this] (const juce::File& f)
    {
        proc.loadImpulseResponse (f);
        if (f.existsAsFile())
            if (auto* cab = proc.apvts.getParameter ("cab")) cab->setValueNotifyingHost (1.0f); // picking a cab means you want it
    };
    addAndMakeVisible (cabPicker);

    // ---- PRE pedals (signal flows left to right into the amp) ----
    auto tile = [&] (std::vector<std::unique_ptr<PedalTile>>& row, const char* name, const char* onId, juce::uint32 colour)
    {
        row.push_back (std::make_unique<PedalTile> (s, name, onId, juce::Colour (colour)));
        addAndMakeVisible (*row.back());
        return row.back().get();
    };
    auto* t = tile (preTiles, "COMP", "comp_on", 0xff3d8bfd);       t->addKnob ("comp_sustain", "SUST"); t->addKnob ("comp_level", "LEVEL");
    t = tile (preTiles, "OCTAVE", "oct_on", 0xffa66cff);            t->addKnob ("oct_sub", "SUB"); t->addKnob ("oct_up", "UP"); t->addKnob ("oct_dry", "DRY");
    t = tile (preTiles, "AUTOWAH", "wah_on", 0xfff2b134);           t->addKnob ("wah_sens", "SENS"); t->addKnob ("wah_reso", "RESO"); t->addKnob ("wah_mix", "MIX");
    t = tile (preTiles, "TS BOOST", "boost", 0xff4caf50);           t->addKnob ("drive", "DRIVE");
    t = tile (preTiles, "NAM PEDAL", "pedal_on", 0xffe0572a);       t->addKnob ("pedal_level", "LEVEL");
    pedalPicker.getCurrent = [this] { return proc.getPedalFile(); };
    pedalPicker.onPick = [this] (const juce::File& f)
    {
        proc.loadPedal (f);
        if (auto* on = proc.apvts.getParameter ("pedal_on")) on->setValueNotifyingHost (f.existsAsFile() ? 1.0f : 0.0f);
    };
    t->setExtra (&pedalPicker);
    t = tile (preTiles, "CHORUS", "chorus_on", 0xff26c6da);         t->addKnob ("chorus_rate", "RATE"); t->addKnob ("chorus_depth", "DEPTH"); t->addKnob ("chorus_mix", "MIX");
    t = tile (preTiles, "FLANGER", "flanger_on", 0xffec407a);       t->addKnob ("flanger_rate", "RATE"); t->addKnob ("flanger_depth", "DEPTH"); t->addKnob ("flanger_regen", "REGEN");
    t = tile (preTiles, "LIMITER", "limit_on", 0xff90a4ae);         t->addKnob ("limit_ceiling", "CEIL"); t->addKnob ("limit_level", "LEVEL");

    // ---- POST pedals ----
    t = tile (postTiles, "GRAPHIC EQ", "geq_on", 0xffb0bec5);
    for (auto [id, label] : { std::pair { "geq_100", "100" }, { "geq_200", "200" }, { "geq_400", "400" }, { "geq_800", "800" },
                              { "geq_1k6", "1.6k" }, { "geq_3k2", "3.2k" }, { "geq_6k4", "6.4k" }, { "geq_level", "LEVEL" } })
        t->addKnob (id, label, true);
    t = tile (postTiles, "DELAY", "delay_on", 0xff26a69a);          t->addKnob ("delay_time", "TIME"); t->addKnob ("delay_fb", "REPEATS"); t->addKnob ("delay_mix", "MIX");
    tapButton.setTooltip ("Tap twice or more in time to set the delay");
    tapButton.onClick = [this] { tapTempo(); };
    t->setExtra (&tapButton);
    t = tile (postTiles, "REVERB", "rev_on", 0xff7986cb);           t->addKnob ("rev_size", "SIZE"); t->addKnob ("rev_tone", "TONE"); t->addKnob ("rev_mix", "MIX");
    reverbPicker.getCurrent = [this] { return proc.getReverbFile(); };
    reverbPicker.onPick = [this] (const juce::File& f) { proc.loadReverbIR (f); };
    t->setExtra (&reverbPicker);

    setSize (kWidth, kHeight);
    applyTheme();
    timerCallback();
    startTimerHz (10);
}

SimpleAmpEditor::~SimpleAmpEditor()
{
    stopTimer();
    tuner = nullptr;
    saveDialog = nullptr;
    setLookAndFeel (nullptr);
}

void SimpleAmpEditor::toggleTuner()
{
    tuner->setVisible (! tuner->isVisible());
    tunerButton.setToggleState (tuner->isVisible(), juce::dontSendNotification);
    if (tuner->isVisible()) tuner->toFront (false);
}

void SimpleAmpEditor::applyTheme()
{
    themeSeen = sa_ui::themeVersion();
    lnf.applyTheme();
    for (auto* l : { &ampLabel, &cabLabel }) l->setColour (juce::Label::textColourId, sa_ui::pal().text);
    darkButton.setToggleState (! sa_ui::isSketch(), juce::dontSendNotification);
    sketchButton.setToggleState (sa_ui::isSketch(), juce::dontSendNotification);
    sendLookAndFeelChange();
    repaint();
}

void SimpleAmpEditor::refreshPresetList()
{
    userPresets = SimpleAmpProcessor::findUserPresets();
    presetBox.clear (juce::dontSendNotification);
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 101 + i);
    }
    presetBox.addSectionHeading ("Factory (amp only)");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
}

void SimpleAmpEditor::savePreset()
{
    const auto current = proc.getUserPreset();
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset",
                                                      "Name it, e.g. \"Meshuggah rhythm\". Saving with an existing name updates that preset.",
                                                      juce::MessageBoxIconType::NoIcon, this);
    saveDialog->setLookAndFeel (&lnf);
    saveDialog->addTextEditor ("name", current.existsAsFile() ? current.getFileNameWithoutExtension() : juce::String ("My preset"));
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        const auto name = saveDialog->getTextEditorContents ("name");
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SimpleAmpEditor> (this)]
                                         { if (safe != nullptr) safe->saveDialog = nullptr; });
        if (result != 1 || name.trim().isEmpty()) return;
        if (! proc.saveUserPreset (name))
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Could not save").withMessage ("Couldn't write the preset file.")
                                              .withButton ("OK"), nullptr);
            return;
        }
        refreshPresetList();
    }), false);
    saveDialog->getTextEditor ("name")->selectAll();
}

void SimpleAmpEditor::deletePreset()
{
    const auto file = proc.getUserPreset();
    if (! file.existsAsFile()) return;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Delete preset")
                                      .withMessage ("Move \"" + file.getFileNameWithoutExtension() + "\" to the Trash?")
                                      .withButton ("Delete")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [this, file] (int result)
                                  {
                                      if (result != 1) return;
                                      file.moveToTrash();
                                      proc.forgetUserPreset();
                                      refreshPresetList();
                                  });
}

void SimpleAmpEditor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    while (taps.size() > 4) taps.remove (0);
    if (taps.size() < 2) return;

    const double ms = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
    if (auto* time = proc.apvts.getParameter ("delay_time"))
        time->setValueNotifyingHost (time->convertTo0to1 ((float) juce::jlimit (20.0, 1500.0, ms)));
    if (auto* on = proc.apvts.getParameter ("delay_on")) on->setValueNotifyingHost (1.0f);
}

void SimpleAmpEditor::timerCallback()
{
    if (themeSeen != sa_ui::themeVersion()) applyTheme(); // switched in another window
    for (auto& t : preTiles) t->refresh();
    for (auto& t : postTiles) t->refresh();

    if (timerTicks++ % 30 == 0) // every 3 s: pick up files dropped into the folders
        for (auto* p : { &ampPicker, &pedalPicker, &cabPicker, &reverbPicker }) p->refresh();
    if (timerTicks % 3 != 0) return;
    ampPicker.refresh (false); cabPicker.refresh (false); // follow changes made elsewhere (tone menu, presets)

    const bool cabOn = proc.apvts.getRawParameterValue ("cab")->load() > 0.5f;
    cabPicker.setAlpha (cabOn ? 1.0f : 0.55f);
    cabPicker.setTooltip (cabOn ? "Cabinet impulse responses from the cabs folder. Pick one to use it."
                                : "Cab is off: the amp model is a full rig that includes the cab. Picking a cab turns it on.");
    ecoButton.setEnabled (proc.isModelSlimmable());

    const double sr = proc.getHostRate();
    const int block = proc.getHostBlock();
    juce::String st;
    if (sr > 0)
    {
        st << juce::String (sr / 1000.0, 1) << " kHz  |  " << block << " samples ("
           << juce::String (1000.0 * block / sr, 1) << " ms)  |  CPU " << juce::roundToInt (proc.getCpuPercent()) << "%";
        if (std::abs (sr - 48000.0) > 0.5) st << "  |  set 48 kHz to skip resampling";
    }
    const auto err = proc.getStatus();
    if (err.isNotEmpty()) st = err;
    statusLabel.setText (st, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, err.isNotEmpty() ? sa_ui::pal().accent : sa_ui::pal().dim);

    if (timerTicks % 30 == 0 && SimpleAmpProcessor::findUserPresets() != userPresets) refreshPresetList();
    const int wantedId = proc.getUserPreset().existsAsFile() ? 101 + userPresets.indexOf (proc.getUserPreset())
                                                             : proc.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != wantedId) presetBox.setSelectedId (wantedId, juce::dontSendNotification);
    deleteButton.setEnabled (proc.getUserPreset().existsAsFile());
}

void SimpleAmpEditor::paint (juce::Graphics& g)
{
    const auto& p = sa_ui::pal();
    auto sectionLabel = [&] (juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (p.dim);
        g.setFont (sa_ui::font (14.5f, true));
        g.drawText (text, area.getX() + 2, area.getY() - 20, 500, 19, juce::Justification::centredLeft);
    };

    if (isSketch())
    {
        sk::paper (g, getLocalBounds().toFloat(), sk::originIn (*this));
        const auto title = sa_ui::font (25.0f, true);
        sk::text (g, "SIMPLEAMP", { 18.0f, 6.0f, 200.0f, 34.0f }, juce::Justification::centredLeft, title, p.accent);
        g.setColour (p.dim);
        g.setFont (sa_ui::font (12.0f));
        g.drawText ("5150 / NAM", 26 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (title, "SIMPLEAMP")), 8, 100, 34,
                    juce::Justification::centredLeft);
        sectionLabel (preArea, "PRE  (into the amp, left to right)");
        sectionLabel (postArea, "POST  (after amp and cab)");

        // The amp, drawn like the app icon: pencil outline, a wash of orange, and a red pilot light top right.
        const auto amp = ampArea.toFloat().reduced (1.5f);
        sk::hatchBox (g, amp, 8.0f, p.accent, 5150u, 0.2f);
        sk::box (g, amp, 8.0f, 5150u, 1.8f);
        juce::Path light;
        light.addEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ amp.getRight() - 13.0f, amp.getY() + 13.0f }));
        sk::hatch (g, light, juce::Colour (0xffe0302a), 7u, 2.5f, 1.4f, true);
        sk::outline (g, light, 8u, 1.1f);
        return;
    }

    g.fillAll (p.bg);

    g.setColour (p.accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("SIMPLEAMP", 18, 8, 200, 32, juce::Justification::centredLeft);
    g.setColour (p.dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("5150 / NAM", 160, 8, 100, 34, juce::Justification::centredLeft);

    sectionLabel (preArea, "PRE  (into the amp, left to right)");
    sectionLabel (postArea, "POST  (after amp and cab)");

    g.setColour (p.panel);
    g.fillRoundedRectangle (ampArea.toFloat(), 8.0f);
    g.setColour (p.accent.withAlpha (0.5f));
    g.drawRoundedRectangle (ampArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void SimpleAmpEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (36);
    top.removeFromLeft (250);
    tunerButton.setBounds (top.removeFromLeft (84).reduced (2, 4));
    top.removeFromLeft (8);
    darkButton.setBounds (top.removeFromLeft (54).reduced (0, 4));
    sketchButton.setBounds (top.removeFromLeft (64).reduced (0, 4));
    inputChBox.setBounds (top.removeFromRight (80).reduced (2, 4));
    top.removeFromRight (10);
    deleteButton.setBounds (top.removeFromRight (64).reduced (2, 4));
    saveButton.setBounds (top.removeFromRight (56).reduced (2, 4));
    presetBox.setBounds (top.removeFromRight (std::min (250, top.getWidth() - 8)).reduced (2, 4));

    statusLabel.setBounds (r.removeFromBottom (20));

    // PRE row
    r.removeFromTop (21);
    preArea = r.removeFromTop (128);
    {
        auto row = preArea;
        const int gap = 6, w = (row.getWidth() - gap * ((int) preTiles.size() - 1)) / (int) preTiles.size();
        for (auto& t : preTiles) { t->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    }

    // AMP
    r.removeFromTop (8);
    ampArea = r.removeFromTop (200);
    {
        auto a = ampArea.reduced (10, 8);
        auto rows = a.removeFromBottom (58);
        auto toggles = a.removeFromRight (80);
        cabButton.setBounds (toggles.removeFromTop (toggles.getHeight() / 2).reduced (4));
        ecoButton.setBounds (toggles.reduced (4));
        const int w = a.getWidth() / (int) ampKnobs.size();
        for (auto& k : ampKnobs)
        {
            auto col = a.removeFromLeft (w);
            k.label.setBounds (col.removeFromTop (21));
            k.slider.setBounds (col.reduced (4, 0));
        }

        auto row1 = rows.removeFromTop (29);
        ampLabel.setBounds (row1.removeFromLeft (46));
        ampPicker.setBounds (row1.removeFromLeft (560).reduced (2, 2));
        auto row2 = rows;
        cabLabel.setBounds (row2.removeFromLeft (46));
        cabPicker.setBounds (row2.removeFromLeft (560).reduced (2, 2));
    }

    tuner->setBounds (preArea.getUnion (ampArea).expanded (0, 4));

    // POST row
    r.removeFromTop (24);
    postArea = r.removeFromTop (128);
    {
        auto row = postArea;
        postTiles[0]->setBounds (row.removeFromLeft (390)); row.removeFromLeft (6);
        postTiles[1]->setBounds (row.removeFromLeft (220)); row.removeFromLeft (6);
        postTiles[2]->setBounds (row);
    }
}
