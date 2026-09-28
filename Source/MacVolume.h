// Hardware volume of audio devices and the Mac's default output, via CoreAudio.
// Used by SimpleAmp Live so the interface's own gain/volume can be set from the app,
// and so the Mac's volume keys control the interface while the app runs.
#pragma once

#include <CoreAudio/CoreAudio.h>
#include <juce_core/juce_core.h>

namespace macvol
{
inline juce::String deviceName (AudioObjectID id)
{
    CFStringRef s = nullptr;
    UInt32 size = sizeof (s);
    AudioObjectPropertyAddress a { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, &s) != noErr || s == nullptr) return {};
    auto n = juce::String::fromCFString (s);
    CFRelease (s);
    return n;
}

inline bool hasChannels (AudioObjectID id, bool input)
{
    AudioObjectPropertyAddress a { kAudioDevicePropertyStreamConfiguration,
                                   input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                   kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (id, &a, 0, nullptr, &size) != noErr || size == 0) return false;
    juce::HeapBlock<char> buf (size);
    auto* list = reinterpret_cast<AudioBufferList*> (buf.get());
    if (AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, list) != noErr) return false;
    UInt32 channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += list->mBuffers[i].mNumberChannels;
    return channels > 0;
}

// Finds a device by the name JUCE shows (CoreAudio names, e.g. "StealthPlug" padded with spaces).
inline AudioObjectID findDevice (const juce::String& name, bool input)
{
    if (name.isEmpty()) return kAudioObjectUnknown;
    AudioObjectPropertyAddress a { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) return kAudioObjectUnknown;
    std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr) return kAudioObjectUnknown;
    for (auto id : ids)
        if (deviceName (id) == name && hasChannels (id, input)) return id;
    for (auto id : ids) // fall back to a trimmed comparison
        if (deviceName (id).trim() == name.trim() && hasChannels (id, input)) return id;
    return kAudioObjectUnknown;
}

// Volume lives on the master element (0) or on each channel (1, 2).
inline juce::Array<UInt32> volumeElements (AudioObjectID id, bool input)
{
    juce::Array<UInt32> els;
    if (id == kAudioObjectUnknown) return els;
    for (UInt32 el = 0; el <= 2; ++el)
    {
        AudioObjectPropertyAddress a { kAudioDevicePropertyVolumeScalar,
                                       input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput, el };
        Boolean settable = false;
        if (AudioObjectHasProperty (id, &a) && AudioObjectIsPropertySettable (id, &a, &settable) == noErr && settable)
        {
            els.add (el);
            if (el == 0) break; // a master control covers all channels
        }
    }
    return els;
}

// Returns -1 when the device has no hardware volume control.
inline float getVolume (AudioObjectID id, bool input)
{
    const auto els = volumeElements (id, input);
    if (els.isEmpty()) return -1.0f;
    float sum = 0;
    for (auto el : els)
    {
        AudioObjectPropertyAddress a { kAudioDevicePropertyVolumeScalar,
                                       input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput, el };
        Float32 v = 0;
        UInt32 size = sizeof (v);
        AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, &v);
        sum += v;
    }
    return sum / (float) els.size();
}

inline void setVolume (AudioObjectID id, bool input, float value)
{
    Float32 v = juce::jlimit (0.0f, 1.0f, value);
    for (auto el : volumeElements (id, input))
    {
        AudioObjectPropertyAddress a { kAudioDevicePropertyVolumeScalar,
                                       input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput, el };
        AudioObjectSetPropertyData (id, &a, 0, nullptr, sizeof (v), &v);
    }
}

inline AudioObjectID getDefaultOutput()
{
    AudioObjectID id = kAudioObjectUnknown;
    UInt32 size = sizeof (id);
    AudioObjectPropertyAddress a { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &id);
    return id;
}

inline bool setDefaultOutput (AudioObjectID id)
{
    if (id == kAudioObjectUnknown) return false;
    AudioObjectPropertyAddress a { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    return AudioObjectSetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, sizeof (id), &id) == noErr;
}
} // namespace macvol
