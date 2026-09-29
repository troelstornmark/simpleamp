// SimpleAmp Live: a standalone app around SimpleAmpProcessor, made for playing.
// Tones bundled in the app (and in ~/Music/SimpleAmp) show up in a menu; the
// audio device, knob settings and last tone are remembered between launches.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MacVolume.h"

namespace
{
const juce::Colour kBar { 0xff101012 }, kAccent { 0xffe0572a }, kText { 0xffe8e6e3 }, kDim { 0xff8d8a86 };

struct Tone
{
    juce::String pack, name;
    juce::File file;
};

bool isBuiltIn (const juce::String& deviceName)
{
    const auto n = deviceName.toLowerCase();
    return n.isEmpty() || n.contains ("built-in") || n.contains ("macbook") || n.contains ("microphone") || n.contains ("speakers");
}

// If an external interface is plugged in but the laptop's mic is selected (or nothing was saved
// yet), switch input and output to the interface. Also makes sure input channels are switched on.
void useInterfaceIfPresent (juce::AudioDeviceManager& dm, bool firstLaunch)
{
    auto setup = dm.getAudioDeviceSetup();
    const bool needsInterface = firstLaunch || isBuiltIn (setup.inputDeviceName) || dm.getCurrentAudioDevice() == nullptr;
    bool changed = false;

    if (auto* type = dm.getCurrentDeviceTypeObject(); type != nullptr && needsInterface)
    {
        type->scanForDevices();
        for (auto& name : type->getDeviceNames (true)) // inputs
            if (! isBuiltIn (name))
            {
                setup.inputDeviceName = name;
                if (type->getDeviceNames (false).contains (name)) setup.outputDeviceName = name;
                setup.sampleRate = 48000.0;
                setup.bufferSize = 64;
                changed = true;
                break;
            }
    }

    // JUCE trims these to what the device actually has.
    if (setup.inputChannels.isZero()) { setup.useDefaultInputChannels = false; setup.inputChannels.setRange (0, 2, true); changed = true; }
    if (setup.outputChannels.isZero()) { setup.useDefaultOutputChannels = false; setup.outputChannels.setRange (0, 2, true); changed = true; }
    if (firstLaunch) { setup.sampleRate = 48000.0; setup.bufferSize = 64; changed = true; }

    if (changed) dm.setAudioDeviceSetup (setup, true);
}

juce::Array<Tone> findTones()
{
    juce::Array<Tone> tones;
    for (auto& f : SimpleAmpProcessor::findAmpFiles())
        tones.add ({ SimpleAmpProcessor::ampGroup (f), SimpleAmpProcessor::displayName (f), f });
    return tones;
}

struct Meter : juce::Component
{
    juce::String label;
    float level = 0.0f; // linear peak, decayed by the owner

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (sa_ui::pal().dim);
        g.setFont (sa_ui::font (11.0f));
        g.drawText (label, r.removeFromLeft (26), juce::Justification::centredLeft);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const auto colour = db > -1.0f ? juce::Colours::red : db > -12.0f ? juce::Colour (0xffe0b12a) : juce::Colour (0xff4caf50);
        if (sa_ui::isSketch())
        {
            const auto seed = (juce::uint32) label.hashCode();
            if (frac > 0.0f) sa_ui::sketch::hatchBox (g, r.reduced (1.5f).withWidth ((r.getWidth() - 3.0f) * frac), 3.0f, colour, seed, 2.0f, true);
            sa_ui::sketch::box (g, r.reduced (1.5f), 3.0f, seed, 1.1f);
            return;
        }
        g.setColour (juce::Colour (0xff2a2a2e));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 3.0f);
    }
};
} // namespace

//==============================================================================
class LiveComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    LiveComponent (juce::AudioDeviceManager& dm, SimpleAmpProcessor& p, juce::PropertiesFile& props)
        : deviceManager (dm), proc (p), settings (props), editor (p)
    {
        setLookAndFeel (&lnf); // the top bar follows the dark / sketch look too (the editor has its own)
        addAndMakeVisible (editor);

        audioButton.onClick = [this] { showAudioSettings(); };
        folderButton.onClick = []
        {
            SimpleAmpProcessor::createLibraryFolders();
            SimpleAmpProcessor::defaultFolder().startAsProcess(); // opens it in Finder
        };
        folderButton.setTooltip ("~/Music/SimpleAmp: drop amps, pedals, cabs and reverbs here. They appear within a few seconds.");
        addAndMakeVisible (folderButton);
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (+1); };
        for (auto* b : { &audioButton, &prevButton, &nextButton }) addAndMakeVisible (*b);

        toneBox.setTextWhenNothingSelected ("Choose a tone...");
        toneBox.onChange = [this] { selectTone (toneBox.getSelectedId() - 1); };
        addAndMakeVisible (toneBox);

        inMeter.label = "IN"; outMeter.label = "OUT";
        addAndMakeVisible (inMeter); addAndMakeVisible (outMeter);

        // The interface's own hardware gain / headphone volume (same controls as Audio MIDI Setup).
        for (auto* sl : { &gainSlider, &volSlider })
        {
            sl->setSliderStyle (juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl->setRange (0.0, 100.0, 1.0);
            sl->setPopupDisplayEnabled (true, true, this);
            sl->setTextValueSuffix (" %");
            sl->setColour (juce::Slider::trackColourId, kAccent);
            addAndMakeVisible (*sl);
        }
        gainSlider.setTooltip ("Interface input gain (hardware). Same as Input in Audio MIDI Setup.");
        volSlider.setTooltip ("Interface output / headphone volume (hardware). The Mac volume keys control it too.");
        gainSlider.onValueChange = [this] { macvol::setVolume (inputId, true, (float) gainSlider.getValue() / 100.0f); };
        volSlider.onValueChange = [this] { macvol::setVolume (outputId, false, (float) volSlider.getValue() / 100.0f); };
        for (auto* l : { &gainLabel, &volLabel })
        {
            l->setFont (juce::FontOptions (11.0f));
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
        }
        gainLabel.setText ("GAIN", juce::dontSendNotification);
        volLabel.setText ("VOL", juce::dontSendNotification);

        warning.setColour (juce::Label::textColourId, kAccent);
        warning.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        warning.setJustificationType (juce::Justification::centredRight);
        addChildComponent (warning);
        deviceManager.addChangeListener (this);
        guardAgainstFeedback();

        rebuildToneList (true);
        updateDeviceVolumes();
        routeVolumeKeys();
        setWantsKeyboardFocus (true);
        setSize (editor.getWidth(), editor.getHeight() + kBarHeight);
        startTimerHz (30);
    }

    ~LiveComponent() override
    {
        deviceManager.removeChangeListener (this);
        restoreDefaultOutput();
        setLookAndFeel (nullptr);
    }

    // Rebuilds the menu when files were added or removed. On first call, selects the last-used tone.
    void rebuildToneList (bool initial)
    {
        auto found = findTones();
        auto sameFiles = [&]
        {
            if (found.size() != tones.size()) return false;
            for (int i = 0; i < found.size(); ++i) if (found[i].file != tones[i].file) return false;
            return true;
        };
        if (! initial && sameFiles()) return;

        const auto current = juce::isPositiveAndBelow (toneBox.getSelectedId() - 1, tones.size())
                               ? tones[toneBox.getSelectedId() - 1].file : juce::File();
        tones = found;
        toneBox.clear (juce::dontSendNotification);
        juce::String lastPack;
        for (int i = 0; i < tones.size(); ++i)
        {
            if (tones[i].pack != lastPack) { toneBox.addSectionHeading (tones[i].pack); lastPack = tones[i].pack; }
            toneBox.addItem (tones[i].name, i + 1);
        }

        if (initial)
        {
            const auto last = settings.getValue ("tone");
            int index = 0;
            for (int i = 0; i < tones.size(); ++i)
                if (tones[i].file.getFullPathName() == last) index = i;
            if (! tones.isEmpty()) toneBox.setSelectedId (index + 1, juce::sendNotificationSync);
        }
        else
        {
            for (int i = 0; i < tones.size(); ++i)
                if (tones[i].file == current) toneBox.setSelectedId (i + 1, juce::dontSendNotification);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bar = getLocalBounds().removeFromTop (kBarHeight).toFloat();
        if (sa_ui::isSketch())
        {
            sa_ui::sketch::paper (g, bar);
            juce::Path rule;
            rule.startNewSubPath (8.0f, bar.getBottom() - 2.0f);
            rule.lineTo (bar.getRight() - 8.0f, bar.getBottom() - 2.0f);
            sa_ui::sketch::outline (g, rule, 52u, 1.2f);
        }
        else
        {
            g.fillAll (kBar);
        }
        if (tones.isEmpty())
        {
            g.setColour (kAccent);
            g.setFont (sa_ui::font (15.0f));
            g.drawText ("No tones found: click Open tones folder and put .nam files in amps/", getLocalBounds().removeFromTop (kBarHeight),
                        juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (kBarHeight).reduced (10, 9);
        audioButton.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (6);
        folderButton.setBounds (bar.removeFromLeft (130));
        bar.removeFromLeft (10);
        auto meters = bar.removeFromRight (290);
        auto top = meters.removeFromTop (meters.getHeight() / 2);
        inMeter.setBounds (top.removeFromLeft (140).reduced (0, 2));
        gainLabel.setBounds (top.removeFromLeft (40));
        gainSlider.setBounds (top);
        outMeter.setBounds (meters.removeFromLeft (140).reduced (0, 2));
        volLabel.setBounds (meters.removeFromLeft (40));
        volSlider.setBounds (meters);
        bar.removeFromRight (10);
        nextButton.setBounds (bar.removeFromRight (34));
        prevButton.setBounds (bar.removeFromRight (34));
        bar.removeFromRight (4);
        toneBox.setBounds (bar);
        editor.setBounds (r);
        warning.setBounds (editor.getBounds().removeFromBottom (30).withTrimmedLeft (300).withTrimmedRight (16));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey) { step (-1); return true; }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey) { step (+1); return true; }
        if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T') { editor.toggleTuner(); return true; }
        return false;
    }

private:
    static constexpr int kBarHeight = 52;

    void syncToneBox()
    {
        const auto current = proc.getModelFile();
        for (int i = 0; i < tones.size(); ++i)
            if (tones[i].file == current && toneBox.getSelectedId() != i + 1)
            {
                toneBox.setSelectedId (i + 1, juce::dontSendNotification);
                settings.setValue ("tone", current.getFullPathName());
            }
    }

    void step (int delta)
    {
        if (tones.isEmpty()) return;
        const int cur = toneBox.getSelectedId() - 1;
        const int next = (cur + delta + tones.size()) % tones.size();
        toneBox.setSelectedId (next + 1, juce::sendNotificationSync);
    }

    void selectTone (int index)
    {
        if (! juce::isPositiveAndBelow (index, tones.size())) return;
        const auto& t = tones.getReference (index);
        proc.selectAmp (t.file);
        settings.setValue ("tone", t.file.getFullPathName());
    }

    // Device selector plus the "volume keys" option.
    struct AudioSettingsPanel : juce::Component
    {
        AudioSettingsPanel (LiveComponent& o)
            : owner (o), selector (o.deviceManager, 1, 2, 2, 2, false, false, false, false)
        {
            addAndMakeVisible (selector);
            keys.setToggleState (o.settings.getBoolValue ("volumeKeys", true), juce::dontSendNotification);
            keys.setColour (juce::ToggleButton::textColourId, kText);
            keys.onClick = [this] { owner.settings.setValue ("volumeKeys", keys.getToggleState()); owner.routeVolumeKeys(); };
            addAndMakeVisible (keys);
            setSize (500, 460);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            keys.setBounds (r.removeFromBottom (40).reduced (16, 8));
            selector.setBounds (r);
        }
        LiveComponent& owner;
        juce::AudioDeviceSelectorComponent selector;
        juce::ToggleButton keys { "Mac volume keys control the interface (while SimpleAmp Live is open)" };
    };

    void showAudioSettings()
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new AudioSettingsPanel (*this));
        o.dialogTitle = "Audio settings (48000 Hz, 64 samples recommended)";
        o.dialogBackgroundColour = juce::Colour (0xff232327);
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    // Fires on device changes, including an interface being plugged in or out.
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (isBuiltIn (deviceManager.getAudioDeviceSetup().inputDeviceName))
            juce::MessageManager::callAsync ([this, safe = juce::Component::SafePointer<LiveComponent> (this)]
            {
                if (safe != nullptr) useInterfaceIfPresent (deviceManager, false);
            });
        guardAgainstFeedback();
        updateDeviceVolumes();
        routeVolumeKeys();
    }

    // Finds the current interface's CoreAudio devices and shows their hardware volume controls.
    void updateDeviceVolumes()
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        inputId = macvol::findDevice (setup.inputDeviceName, true);
        outputId = macvol::findDevice (setup.outputDeviceName, false);
        auto show = [] (juce::Slider& sl, AudioObjectID id, bool input)
        {
            const float v = macvol::getVolume (id, input);
            sl.setEnabled (v >= 0.0f);
            if (v >= 0.0f && ! sl.isMouseButtonDown()) sl.setValue (std::round (v * 100.0f), juce::dontSendNotification);
        };
        show (gainSlider, inputId, true);
        show (volSlider, outputId, false);
    }

    // While the app runs, make the interface the Mac's sound output so the volume keys and the
    // menu-bar volume control it. The previous output comes back when the app quits.
    void routeVolumeKeys()
    {
        const bool wanted = settings.getBoolValue ("volumeKeys", true);
        const auto outName = deviceManager.getAudioDeviceSetup().outputDeviceName;
        if (! wanted || isBuiltIn (outName) || outputId == kAudioObjectUnknown) { restoreDefaultOutput(); return; }

        const auto current = macvol::getDefaultOutput();
        if (current == outputId) return;
        if (previousDefaultOutput == kAudioObjectUnknown) previousDefaultOutput = current;
        macvol::setDefaultOutput (outputId);
    }

    void restoreDefaultOutput()
    {
        if (previousDefaultOutput != kAudioObjectUnknown) macvol::setDefaultOutput (previousDefaultOutput);
        previousDefaultOutput = kAudioObjectUnknown;
    }

    // Never run a 5150 on the laptop microphone: it would feed back through the speakers.
    // Mute inside the amp rather than changing the device setup, so switching devices always works.
    void guardAgainstFeedback()
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        auto* dev = deviceManager.getCurrentAudioDevice();
        const bool micInput = isBuiltIn (setup.inputDeviceName);
        const bool noChannels = dev == nullptr || dev->getActiveInputChannels().isZero();
        proc.setInputMuted (micInput);

        juce::String text;
        if (micInput || setup.inputDeviceName.isEmpty()) text = "No guitar input: choose your interface in Audio settings";
        else if (noChannels) text = "Input channel is off: tick it in Audio settings";
        warning.setText (text, juce::dontSendNotification);
        warning.setVisible (text.isNotEmpty());
    }

    void timerCallback() override
    {
        if (themeSeen != sa_ui::themeVersion()) // the look was switched in the editor
        {
            themeSeen = sa_ui::themeVersion();
            lnf.applyTheme();
            sendLookAndFeelChange();
            repaint();
        }
        if (++ticks % 90 == 0) rebuildToneList (false); // every 3 s: pick up new files
        if (ticks % 10 == 0) { syncToneBox(); updateDeviceVolumes(); } // follows Audio MIDI Setup and volume keys
        const float decay = 0.85f;
        inMeter.level = std::max (proc.takeInputPeak(), inMeter.level * decay);
        outMeter.level = std::max (proc.takeOutputPeak(), outMeter.level * decay);
        inMeter.repaint(); outMeter.repaint();
    }

    int ticks = 0, themeSeen = sa_ui::themeVersion();
    sa_ui::Lnf lnf;
    juce::AudioDeviceManager& deviceManager;
    SimpleAmpProcessor& proc;
    juce::PropertiesFile& settings;
    SimpleAmpEditor editor;
    juce::Array<Tone> tones;
    juce::TextButton audioButton { "Audio settings" }, folderButton { "Open tones folder" }, prevButton { "<" }, nextButton { ">" };
    juce::ComboBox toneBox;
    Meter inMeter, outMeter;
    juce::Slider gainSlider, volSlider;
    juce::Label gainLabel, volLabel;
    AudioObjectID inputId = kAudioObjectUnknown, outputId = kAudioObjectUnknown, previousDefaultOutput = kAudioObjectUnknown;
    juce::Label warning;
};

//==============================================================================
class SimpleAmpLiveApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SimpleAmp Live"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains ("--selftest"); }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "SimpleAmp Live";
        opts.filenameSuffix = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (opts);

        SimpleAmpProcessor::createLibraryFolders(); // ~/Music/SimpleAmp/{amps,pedals,cabs,reverbs} + guide
        processor = std::make_unique<SimpleAmpProcessor>();
        if (auto state = settings->getValue ("pluginState"); state.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (state)) processor->setStateInformation (mb.getData(), (int) mb.getSize());
        }

        // Audio: prefer 48 kHz / 64 samples; remember whatever the user picks.
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;
        preferred.bufferSize = 64;
        auto savedDevice = settings->getXmlValue ("audioDevice");
        deviceManager.initialise (2, 2, savedDevice.get(), true, {}, savedDevice == nullptr ? &preferred : nullptr);
        useInterfaceIfPresent (deviceManager, savedDevice == nullptr);

        if (getCommandLineParameters().contains ("--sketch")) sa_ui::setSketch (true);  // self-test / screenshots
        if (getCommandLineParameters().contains ("--dark")) sa_ui::setSketch (false);
        player.setProcessor (processor.get());
        deviceManager.addAudioCallback (&player);

        window = std::make_unique<MainWindow> (getApplicationName(),
                                               new LiveComponent (deviceManager, *processor, *settings));

        // --selftest <png>: run for 3 s, report the audio state, save a picture of the window, quit.
        const auto args = getCommandLineParameterArray();
        if (const int i = args.indexOf ("--selftest"); i >= 0 && i + 1 < args.size())
        {
            const juce::File png (args[i + 1]);
            if (args.contains ("--tuner"))
                juce::Timer::callAfterDelay (1000, [this]
                {
                    if (auto* c = window->getContentComponent()) c->keyPressed (juce::KeyPress ('t', 0, 't'));
                });
            juce::Timer::callAfterDelay (3000, [this, png]
            {
                auto* dev = deviceManager.getCurrentAudioDevice();
                if (dev != nullptr)
                    std::printf ("device type: %s | input names: %s | active in chans: %s (of %d) | active out: %s | input peak %.1f dBFS\n",
                                 dev->getTypeName().toRawUTF8(),
                                 dev->getInputChannelNames().joinIntoString (",").toRawUTF8(),
                                 dev->getActiveInputChannels().toString (2).toRawUTF8(), dev->getInputChannelNames().size(),
                                 dev->getActiveOutputChannels().toString (2).toRawUTF8(),
                                 juce::Decibels::gainToDecibels (processor->takeInputPeak(), -150.0f));
                std::printf ("device: %s | in: %s | rate %.0f | block %d | model: %s | slimmable %d | cpu %.1f%% | %s\n",
                             dev ? dev->getName().toRawUTF8() : "none",
                             deviceManager.getAudioDeviceSetup().inputDeviceName.toRawUTF8(),
                             dev ? dev->getCurrentSampleRate() : 0.0, dev ? dev->getCurrentBufferSizeSamples() : 0,
                             processor->getModelName().toRawUTF8(), (int) processor->isModelSlimmable(),
                             deviceManager.getCpuUsage() * 100.0, processor->getStatus().toRawUTF8());
                std::fflush (stdout);
                if (auto* c = window->getContentComponent())
                {
                    juce::FileOutputStream out (png);
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (c->createComponentSnapshot (c->getLocalBounds()), out); }
                }
                quit();
            });
        }
    }

    void shutdown() override
    {
        if (processor == nullptr) return; // a second launch that never initialised
        if (auto xml = deviceManager.createStateXml()) settings->setValue ("audioDevice", xml.get());
        juce::MemoryBlock mb;
        processor->getStateInformation (mb);
        settings->setValue ("pluginState", mb.toBase64Encoding());
        settings->saveIfNeeded();

        window = nullptr;
        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        deviceManager.closeAudioDevice();
        processor = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    struct MainWindow : juce::DocumentWindow
    {
        MainWindow (const juce::String& name, juce::Component* content)
            : DocumentWindow (name, kBar, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<SimpleAmpProcessor> processor;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SimpleAmpLiveApp)
