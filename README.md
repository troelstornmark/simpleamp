# SimpleAmp

A small, low-CPU guitar amp plugin (AU / VST3 / Standalone) for playing live in **UA LUNA**
on an Intel Mac. It runs [Neural Amp Modeler](https://www.neuralampmodeler.com) (`.nam`) captures,
so it can sound like a real Peavey 5150 into a Mesa 4x12.

```
PRE:   Guitar → Input → Gate → Comp → Octave → Autowah → TS Boost → NAM pedal → Chorus → Flanger → Limiter
AMP:   → NAM amp model → Cab IR → Bass/Mid/Treble
POST:  → Graphic EQ → Delay → Reverb (stereo) → Output
```

- The pre chain and amp are mono, and the delay and reverb add stereo. No oversampling, no reported latency at 48 kHz, and no GPU-heavy UI.
- Until you load a `.nam` model, a built-in amp and cab (filters only) let you play right away.
- The gate sits before the amp and closes fast, for tight staccato riffs.
- **Eco**: A2 models from Tone3000 contain a Full and a Lite network. Eco switches to the Lite one.

## Install

```sh
./scripts/build.sh
```

The script installs cmake if it's missing, fetches JUCE 8.0.15 and NeuralAmpModelerCore (pinned), and builds.
Then it installs:

| Format | Location |
|---|---|
| AU (LUNA on Mac) | `~/Library/Audio/Plug-Ins/Components/SimpleAmp.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/SimpleAmp.vst3` |
| Standalone app | `/Applications/SimpleAmp.app` |

Only the Xcode Command Line Tools are needed, not full Xcode.

## SimpleAmp Live (standalone app)

`/Applications/SimpleAmp Live.app` is the easiest way to play without a DAW.

- **Tones are built in.** Every `.nam` under `./tones` (one subfolder per pack) is copied into the app when you run
  `./scripts/build.sh`. The app also lists anything in `~/Music/SimpleAmp`.
- **Switch tones** with the menu, the ◀ ▶ buttons, or the arrow keys.
- **Automatic switches.** Full-rig captures (cab included) turn **Cab** off. Captures that already include a boost pedal
  (Maxon, MXR, OD808, SD-1, Rat, OCD, or a "Boosted" pack) turn **Boost** off, so you don't double-boost.
- **Audio settings** defaults to 48 kHz and 64 samples. On first launch the app picks your Scarlett or StealthPlug if it's plugged in.
- **IN / OUT meters** help you set the interface gain. IN should peak in the yellow and never hit red.
- **Feedback protection.** The app never amplifies the laptop's built-in microphone. If that's the selected input, it's
  muted and a warning asks you to choose your interface.
- **Remembers everything.** Device, knobs, and last tone are saved in `~/Library/Application Support/SimpleAmp Live.settings`.

The first time you open it, macOS asks for microphone access. Allow it: that's how the app hears your guitar input.

Measured with your tones at 48 kHz / 64 samples: about **10 %** of one core, or about **2 %** with **Eco**. Eco keeps the
same loudness, so you can switch freely.

## Pedalboard

The pedals are laid out in signal order. The **PRE** row feeds the amp from left to right; the **POST** row comes after
the amp and cab. To switch a pedal on or off, click its LED or its name. Switching fades over 10 ms, so it doesn't click,
and a pedal that's off uses no CPU. Drag a knob to see its value, and double-click it to reset.

| Pedal | Knobs | Notes |
|---|---|---|
| COMP | Sustain, Level | Dyna-Comp style. For clean and funk tones; mostly off for metal. |
| OCTAVE | Sub, Up, Dry | Analog-style (OC-2 idea). It tracks single notes, not chords. Sub + drive = huge riffs. |
| AUTOWAH | Sens, Reso, Mix | Envelope filter: pick harder, the filter opens. |
| TS BOOST | Drive | The tight Tube Screamer in front of the 5150. Leave it off when the capture is already boosted. |
| NAM PEDAL | Level + file menu | Any pedal capture from `pedals/`. Level is the pedal's volume knob, so push it to hit the amp harder. |
| CHORUS | Rate, Depth, Mix | Mono chorus before the amp (like a CE-2 on the floor). |
| FLANGER | Rate, Depth, Regen | Regen below 0 gives the hollow, negative-feedback flavour. |
| LIMITER | Ceiling, Level | Keeps peaks into the amp even, which helps fast palm-mutes stay consistent. |
| GRAPHIC EQ | 100 Hz to 6.4 kHz, Level | GE-7 bands, ±15 dB. The classic metal move is to cut 400 Hz and nudge 800 Hz to 1.6k. |
| DELAY | Time, Repeats, Mix, **TAP** | The repeats darken as they fade. TAP sets the time from your taps. The tail rings out when switched off. |
| REVERB | Size, Tone, Mix + file menu | "Built-in room", or any impulse response from `reverbs/`. The tail rings out when switched off. |

**Full board CPU** (every pedal on, your 5150 tone, a NAM pedal and a 1.5 s IR reverb, 48 kHz / 64 samples):
about **14 %** of one core, or about **5 %** with Eco.

## Tuner

Click **TUNER**, or press **T** in SimpleAmp Live. Play one string: you get the note name, a needle, and the offset
in cents. The needle turns green within ±3 cents. The tuner reads your raw guitar signal before any pedals, so drive
and effects don't confuse it. It covers **30 Hz to 1.4 kHz**, which includes drop tunings and 7- and 8-string low strings
(Meshuggah's low F# is 46 Hz). It's accurate to about 1 cent, measured on test tones from 46 Hz to 1.3 kHz.
The output is **muted while the tuner is open**, like a floor tuner; untick *Mute output while tuning* to hear yourself.
Close it with **Done** or **T**. It uses no CPU while closed.

## Presets

- **Save** stores your *whole* setup as a preset: every knob, every pedal's on/off state and settings, and the amp,
  NAM pedal, cab and reverb files. Give it a name; saving with an existing name updates that preset.
- Your presets appear at the top of the preset menu under **My presets**. The **Factory** presets below them only
  set the amp and TS Boost, and leave your pedalboard alone.
- **Delete** moves the selected preset to the Trash, so you can get it back.
- Presets are files in `~/Music/SimpleAmp/presets/` (`.simpleamp`), shared by SimpleAmp Live and the plugin in LUNA.
  Copy them to back them up or move them to another Mac.
- The selected preset is remembered with the app, or with your LUNA session.

## Adding tones, pedals, cabs and reverbs

Click **Open tones folder** in SimpleAmp Live (or open `~/Music/SimpleAmp` in Finder). Drop files into the
matching folder, and they appear in the menus within about 3 seconds, with no restart or rebuild:

```
~/Music/SimpleAmp/
├── amps/        *.nam   amp captures (heads or full rigs). Subfolders become groups in the tone menu.
├── pedals/      *.nam   pedal captures for the NAM PEDAL slot: boost, overdrive, distortion, fuzz, (compressor)
├── cabs/        *.wav   cabinet impulse responses (for head-only captures, with Cab on)
├── reverbs/     *.wav   reverb impulse responses, mono or stereo (the first 3 s are used)
└── HOW TO ADD TONES.txt
```

To **bundle** files inside the app (so it's self-contained), use the same layout in this project's `tones/` folder
and run `./scripts/build.sh`. Any folder other than `pedals/`, `cabs/` and `reverbs/` counts as an amp pack.

**What can be a file, and what can't.** A NAM capture can only learn effects that react the same way every time:
drive, distortion, fuzz, tone shaping, and approximately compressors. Anything that changes over time can't be
captured: an LFO (chorus, flanger), an envelope (autowah), pitch tracking (octaver), or long memory (delay, reverb).
That's why those are built-in pedals. Reverb is the exception that also works as a file, because a reverb's sound
*is* an impulse response.

**Where to get them.**
- Pedal captures: [Tone3000](https://www.tone3000.com), with the Gear filter set to **Pedal** (e.g. search "TS808",
  "SD-1", "Precision Drive" or "Rat"). Download the `.nam` into `pedals/`. Start with the NAM PEDAL Level at 0 dB.
- Reverb IRs: [Voxengo's free IR pack](https://www.voxengo.com/impulses/) (plates, halls and rooms), or the
  [OpenAIR library](https://www.openair.hosted.york.ac.uk/) (real spaces). Put the `.wav` files in `reverbs/`.
- Cab IRs: Tone3000 (Gear filter **IR**), e.g. the Mesa V30 IR linked below. Put them in `cabs/`.

## Get the 5150 + Mesa tone (free, from Tone3000)

Tone3000 needs a free account to download. Put amp `.nam` files in **`~/Music/SimpleAmp/amps/`** and cab `.wav`
files in **`~/Music/SimpleAmp/cabs/`**.

**Option A: full rig (amp + cab in one model). Recommended; simplest.**
[Full Rig Peavey 5150 + Mesa 4x12](https://www.tone3000.com/tones/full-rig-peavey-5150-mesa-4x12-32868)
by jpisoutoftune (~104k downloads). This is a 5150 red channel into a Mesa Oversized 4x12, mic'd with an SM57/SM58.
- **Turn SimpleAmp's `Cab` OFF.** The cab is already inside the model.
- `...Maxon...SM57` has a Maxon OD808 boost built in: **turn `Boost` OFF** for Gojira and Meshuggah.
- `...No boost...SM57` is for Pantera, or use it with SimpleAmp's `Boost` ON.

**Option B: amp head + separate cab IR (more control).**
- Amp: [Peavey 6505 (0.5.2)](https://www.tone3000.com/tones/peavey-6505-052-2014) by arlingtonaudio. It's the
  same circuit as the 5150; use a **Red channel** capture.
- Cab: [The HEAVIEST Mesa Recto V30 IR](https://www.tone3000.com/tones/the-heaviest-mesa-recto-v30-ir-44078).
- `Cab` ON and `Boost` ON.

If a pack offers several sizes, pick **Feather** or **Lite** (or **Nano** if CPU is tight). For A2 files, use the
`Eco` switch instead.

## Interface and LUNA setup (lowest latency)

1. **Audio MIDI Setup** (Applications ▸ Utilities): set the interface to **48,000 Hz**. NAM models are
   48 kHz, so any other rate forces resampling, which costs CPU and adds latency. The plugin's status line warns you
   when you're not at 48k.
2. **LUNA ▸ Settings ▸ Audio**: pick the interface, 48 kHz, **buffer 64** samples (about 1.3 ms). If you hear
   clicks, go to 128.
3. New **mono** audio track ▸ insert **SimpleAmp** (under *Tornmark*) ▸ enable input monitoring on the track.
   LUNA scans new plug-ins when it starts; restart LUNA if SimpleAmp doesn't show up.
4. Set the Input level: hit your hardest chord. The note level should get loud but the plugin output must not clip.
   Use SimpleAmp `Input` to push the amp harder or softer.

**Focusrite Scarlett (2nd gen)** is the better choice for live latency. It's class compliant, so it needs no driver.
Plug into a front input, press **INST**, and set the gain so the ring only flashes red on your hardest hits.
**Turn Direct Monitor OFF**, or you'll hear your dry guitar under the amp. On the 2i2, if you use input 2, set
SimpleAmp's `In 1 / In 2` box to **In 2**.

**IK Multimedia StealthPlug** is class compliant: no driver needed, and it works on macOS 15 (1 input, 2 outputs at
48 kHz). You hear the amp through its headphone jack. To listen through the Mac instead, pick the Mac's output in
Audio settings.

**Interface volume in SimpleAmp Live.** The top bar's **GAIN** and **VOL** sliders are the interface's own hardware
input gain and headphone volume, the same controls as in Audio MIDI Setup, and they stay in sync with it.
macOS volume keys normally control only the Mac's *default* output (usually the built-in speakers). So while
SimpleAmp Live is open, it makes your interface the Mac's sound output. The volume keys and menu-bar slider then
control the StealthPlug or Scarlett, and the previous output comes back when you quit. You can switch this off in
**Audio settings** ("Mac volume keys control the interface").

**Low-CPU tips for the 2015 MacBook Pro:** use the power adapter, close browsers and Dropbox-type apps,
keep one SimpleAmp instance per guitar, and bypass it on tracks you're not playing. The Standalone app skips the DAW
entirely: it's the lightest way to just jam. Go to Options ▸ Audio/MIDI Settings and set 48000 Hz and 64 samples.
The first time you open it, macOS asks for microphone access. Allow it: that's how the app hears your guitar.
JUCE also mutes the input by default, to prevent feedback through laptop speakers. With headphones or monitors,
click the banner's **Settings** button and untick *Mute audio input*. The app remembers the setting.

## Tone starting points (preset menu)

| Preset | Boost | Gate | EQ (B/M/T) | Notes |
|---|---|---|---|---|
| Default 5150 | on, drive 0.20 | -60 dB | 0 / 0 / 0 | |
| Pantera (scooped) | off | -64 dB | +3 / -6 / +3 | Dimebag was scooped but not *dead*; don't go below -6 on mids. |
| Gojira (boosted, flat) | on, drive 0.15 | -60 dB | 0 / +1 / +1 | Tight, flat mids, lots of pick attack. |
| Meshuggah (tight/djent) | on, drive 0.35 | -52 dB | -2 / +3 / +2 | Raise Gate until palm-mute gaps are dead silent. |

Presets never change the Cab switch, because that depends on which model you loaded.

## Controls

| Control | What it does |
|---|---|
| INPUT | Level into the gate and amp (-24 to +24 dB). This is really your gain knob. |
| GATE | Gate threshold. Fully left = off. Raise it until noise between riffs disappears. |
| BASS / MID / TREBLE | The amp's own EQ, after the amp: ±12 dB at 110 Hz / 750 Hz / 3.5 kHz. |
| OUTPUT | Final level, after the delay and reverb. Amp models are normalized to about -18 dB loudness. |
| Cab / Eco | Cab IR (or built-in cab) on/off · smaller network for A2 amp and pedal models. |
| Cab IR menu | Files from `cabs/`, "Built-in cab", or Browse... |

The TS boost's DRIVE knob now lives on the TS BOOST pedal. Presets only set the amp and TS boost, and leave the
rest of the pedalboard alone.

## Measured on this machine (i7-4770HQ, 64-sample buffers)

| Model | CPU (one core) |
|---|---|
| Built-in amp + cab | ~0.5 % |
| NAM LSTM (small example model) | ~1 % |
| NAM WaveNet Standard | ~9 % |
| NAM A2 (Tone3000's current format) | ~10 % |
| A2 slimmable example: full / Eco on | ~4 % / ~1 % |
| Resampling at 44.1 kHz | +0.5 %, +27 samples latency |
| Your 5150 full rig (A2): full / Eco | ~10 % / ~2 % |
| Whole pedalboard on + 5150 + NAM pedal + IR reverb: full / Eco | ~14 % / ~5 % |

Peak memory for a host with two SimpleAmp instances was about 55 MB.

## Tests

```sh
cmake --build build --target SimpleAmpTest && ./build/SimpleAmpTest_artefacts/Release/SimpleAmpTest
```

The tests run the DSP with NAM's example models at 48 and 44.1 kHz. They check for NaN/Inf, that the gate closes
between notes, and that CPU stays under budget. Then they load the **installed** VST3 and AU binaries in a host,
play audio through them, open the editor, and round-trip the state.

## Licenses

NeuralAmpModelerCore is MIT. JUCE is used under its free personal/AGPLv3 terms, which is fine for your own rig.
Tone3000 files are T3K-licensed: free to use, but don't redistribute them, which is why they aren't bundled.
