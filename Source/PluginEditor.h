#pragma once

#include "PluginProcessor.h"
#include "Tuner.h"
#include "Sketch.h"

namespace sa_ui
{
using APVTS = juce::AudioProcessorValueTreeState;

// Look and feel whose rotary knobs use their space well even when small (pedal knobs).
// Follows the current theme (dark or sketch): call applyTheme() after switching.
struct Lnf : juce::LookAndFeel_V4
{
    Lnf() { applyTheme(); }
    void applyTheme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;

    juce::Font getLabelFont (juce::Label& l) override { return themed (l.getFont()); }
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return themed (LookAndFeel_V4::getComboBoxFont (b)); }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override { return themed (LookAndFeel_V4::getTextButtonFont (b, h)); }
    juce::Font getPopupMenuFont() override { return themed (LookAndFeel_V4::getPopupMenuFont()); }
    juce::Font getSliderPopupFont (juce::Slider& s) override { return themed (LookAndFeel_V4::getSliderPopupFont (s)); }
    juce::Font getAlertWindowMessageFont() override { return themed (LookAndFeel_V4::getAlertWindowMessageFont()); }
    juce::Font getAlertWindowTitleFont() override { return themed (LookAndFeel_V4::getAlertWindowTitleFont()); }

private:
    juce::SharedResourcePointer<sketch::Textures> textures; // keeps the paper texture alive while a UI is open
};

// Round LED footswitch.
struct Led : juce::Button
{
    juce::Colour colour;
    explicit Led (juce::Colour c) : juce::Button ("on"), colour (c) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

struct Knob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// A pedal on the board: name + LED on top, knobs below, optional extra row (file menu, tap button).
class PedalTile : public juce::Component
{
public:
    PedalTile (APVTS& state, const juce::String& name, const juce::String& onParamId, juce::Colour colour);

    void addKnob (const juce::String& paramId, const juce::String& label, bool vertical = false);
    void setExtra (juce::Component* c) { extra = c; addAndMakeVisible (c); }
    void refresh(); // repaint when on/off changed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    APVTS& apvts;
    juce::String name;
    juce::Colour colour;
    Led led;
    std::unique_ptr<APVTS::ButtonAttachment> ledAttachment;
    std::vector<std::unique_ptr<Knob>> knobs;
    bool vertical = false, lastOn = false;
    juce::Component* extra = nullptr;
};

// Drop-down of library files of one kind, plus "Browse...".
class FilePicker : public juce::ComboBox
{
public:
    FilePicker (juce::String kind, juce::String noneText, juce::String pattern);
    std::function<void (const juce::File&)> onPick;
    std::function<juce::File()> getCurrent;
    void refresh (bool rescan = true); // rescan folders (if asked) and reflect the current file

private:
    void picked();
    juce::String kind, noneText, pattern;
    juce::Array<juce::File> files;
    juce::File shownCurrent;
    std::unique_ptr<juce::FileChooser> chooser;
};
// Big tuner display. While visible, the processor records the raw input and (optionally) mutes the output.
class TunerPanel : public juce::Component, private juce::Timer
{
public:
    explicit TunerPanel (SimpleAmpProcessor& p);
    ~TunerPanel() override;
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    SimpleAmpProcessor& proc;
    sa::PitchDetector detector;
    std::vector<float> samples = std::vector<float> (4096);
    float shownMidi = 0, shownHz = 0;
    double lastPitchTime = 0;
    bool hasPitch = false;
    juce::ToggleButton muteButton { "Mute output while tuning" };
    juce::TextButton closeButton { "Done" };
};
} // namespace sa_ui

class SimpleAmpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SimpleAmpEditor (SimpleAmpProcessor&);
    ~SimpleAmpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void toggleTuner();
    void applyTheme(); // after the look was switched (here or in another window)

private:
    void timerCallback() override;
    void refreshPresetList();
    void savePreset();
    void deletePreset();
    void tapTempo();

    SimpleAmpProcessor& proc;
    sa_ui::Lnf lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // Amp
    std::array<sa_ui::Knob, 6> ampKnobs; // input, gate, bass, mid, treble, output
    juce::ToggleButton cabButton { "Cab" }, ecoButton { "Eco" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAtt, ecoAtt;
    juce::ComboBox presetBox, inputChBox;
    juce::TextButton tunerButton { "TUNER" }, saveButton { "Save" }, deleteButton { "Delete" };
    juce::TextButton darkButton { "DARK" }, sketchButton { "SKETCH" };
    int themeSeen = -1;
    juce::Array<juce::File> userPresets;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    std::unique_ptr<sa_ui::TunerPanel> tuner;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputChAtt;
    juce::Label ampLabel, cabLabel, statusLabel;

    // Pedalboard
    sa_ui::FilePicker ampPicker { "amps", "Built-in amp", "*.nam" };
    sa_ui::FilePicker pedalPicker { "pedals", "(none)", "*.nam" };
    sa_ui::FilePicker cabPicker { "cabs", "Built-in cab", "*.wav" };
    sa_ui::FilePicker reverbPicker { "reverbs", "Built-in room", "*.wav" };
    juce::TextButton tapButton { "TAP" };
    juce::Array<double> taps;
    std::vector<std::unique_ptr<sa_ui::PedalTile>> preTiles, postTiles;
    juce::Rectangle<int> preArea, ampArea, postArea;
    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleAmpEditor)
};
