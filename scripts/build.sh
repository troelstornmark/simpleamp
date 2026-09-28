#!/bin/bash
# Build SimpleAmp (AU + VST3 + Standalone) and install it for the current user.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/Library/Python/3.9/bin:$PATH"

command -v cmake >/dev/null || python3 -m pip install --user cmake

# Dependencies (pinned)
mkdir -p third_party
[ -d third_party/JUCE ] || git clone -q --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git third_party/JUCE
if [ ! -d third_party/NeuralAmpModelerCore ]; then
  git clone -q https://github.com/sdatkinson/NeuralAmpModelerCore.git third_party/NeuralAmpModelerCore
  git -C third_party/NeuralAmpModelerCore checkout -q 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842
  git -C third_party/NeuralAmpModelerCore submodule update --init --recursive -q
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)" --target SimpleAmp_AU SimpleAmp_VST3 SimpleAmp_Standalone SimpleAmpLive

ART=build/SimpleAmp_artefacts/Release
COMP=~/Library/Audio/Plug-Ins/Components
VST3=~/Library/Audio/Plug-Ins/VST3
mkdir -p "$COMP" "$VST3"
for d in amps pedals cabs reverbs; do mkdir -p "$HOME/Music/SimpleAmp/$d" tones/$d; done

LIVE="build/SimpleAmpLive_artefacts/Release/SimpleAmp Live.app"
for b in "$ART/AU/SimpleAmp.component" "$ART/VST3/SimpleAmp.vst3" "$ART/Standalone/SimpleAmp.app" "$LIVE"; do
  codesign --force --deep -s - "$b"
done

rm -rf "$COMP/SimpleAmp.component" "$VST3/SimpleAmp.vst3" /Applications/SimpleAmp.app "/Applications/SimpleAmp Live.app"
cp -R "$ART/AU/SimpleAmp.component" "$COMP/"
cp -R "$ART/VST3/SimpleAmp.vst3" "$VST3/"
cp -R "$ART/Standalone/SimpleAmp.app" /Applications/
cp -R "$LIVE" /Applications/

# Make macOS re-read the AU list, then validate.
killall -9 AudioComponentRegistrar 2>/dev/null || true
if auval -v aufx SAmp Trnm >/tmp/simpleamp-auval.txt 2>&1; then
  echo "auval: AU VALIDATION SUCCEEDED"
else
  echo "auval: not visible from this shell (normal for SSH/background sessions)."
  echo "       Run 'auval -v aufx SAmp Trnm' in Terminal.app, or just open LUNA."
fi

echo
echo "Installed:"
echo "  AU        $COMP/SimpleAmp.component"
echo "  VST3      $VST3/SimpleAmp.vst3"
echo "  Standalone /Applications/SimpleAmp.app"
echo "  Live app  /Applications/SimpleAmp Live.app   (tones from ./tones bundled inside)"
echo "Tone library: ~/Music/SimpleAmp/{amps,pedals,cabs,reverbs}  (no rebuild needed)"
echo "          or: ./tones/{<packs>,pedals,cabs,reverbs} (bundled into SimpleAmp Live at build)"
