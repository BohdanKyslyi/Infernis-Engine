#pragma once

#include <openal/al.h>
#include <openal/alc.h>
#include <openal/alext.h>
#include <string>
#include <vector>

// No context/source/buffer destruction is permitted on the live settings path.
// Kept independent of the engine so the failure paths can be tested without a level.
namespace SoundDevice {
enum Output { Auto = 0, Stereo, Surround51, Surround71 };

struct Settings {
    std::string device; // Empty means the system default endpoint.
    int hrtf = 1;
    unsigned int output = Auto;
};

enum class ApplyResult { Unchanged, Applied, Unsupported, Failed };

inline bool HasExtension(ALCdevice* device, const char* name) {
    return alcIsExtensionPresent(device, name) == ALC_TRUE;
}

inline bool WantsHRTF(const Settings& settings) {
    // Explicit surround speaker layouts take precedence over headphone processing.
    return settings.hrtf != 0 && settings.output <= Stereo;
}

inline std::vector<ALCint> Attributes(ALCdevice* device, const Settings& settings) {
    std::vector<ALCint> attrs;
    const bool hasHRTF = HasExtension(device, "ALC_SOFT_HRTF");
    const bool hrtf = hasHRTF && WantsHRTF(settings);
    if (hasHRTF) {
        attrs.push_back(ALC_HRTF_SOFT);
        attrs.push_back(hrtf ? ALC_TRUE : ALC_FALSE);
    }
    if (HasExtension(device, "ALC_SOFT_output_mode")) {
        ALCint mode = ALC_ANY_SOFT;
        switch (settings.output) {
        case Stereo: mode = ALC_STEREO_BASIC_SOFT; break;
        case Surround51: mode = ALC_SURROUND_5_1_SOFT; break;
        case Surround71: mode = ALC_SURROUND_7_1_SOFT; break;
        }
        if (hrtf)
            mode = ALC_STEREO_HRTF_SOFT;
        attrs.push_back(ALC_OUTPUT_MODE_SOFT);
        attrs.push_back(mode);
    }
    attrs.push_back(0);
    return attrs;
}

inline ApplyResult Apply(ALCdevice* device, const Settings& previous, const Settings& requested) {
    const bool endpointChanged = previous.device != requested.device;
    const bool hrtfChanged = previous.hrtf != requested.hrtf;
    const bool outputChanged = previous.output != requested.output;
    if (!endpointChanged && !hrtfChanged && !outputChanged)
        return ApplyResult::Unchanged;
    if (!device || requested.output > Surround71)
        return ApplyResult::Failed;
    if ((hrtfChanged && !HasExtension(device, "ALC_SOFT_HRTF")) ||
        (outputChanged && !HasExtension(device, "ALC_SOFT_output_mode")))
        return ApplyResult::Unsupported;

    const auto attrs = Attributes(device, requested);
    alcGetError(device);
    if (endpointChanged) {
        if (!HasExtension(device, "ALC_SOFT_reopen_device"))
            return ApplyResult::Unsupported;
        auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(
            alcGetProcAddress(device, "alcReopenDeviceSOFT"));
        if (!reopen)
            return ApplyResult::Unsupported;
        return reopen(device, requested.device.empty() ? nullptr : requested.device.c_str(), attrs.data())
            ? ApplyResult::Applied : ApplyResult::Failed;
    }

    // alcResetDeviceSOFT is provided by either of these extensions.
    if (!HasExtension(device, "ALC_SOFT_HRTF") &&
        !HasExtension(device, "ALC_SOFT_output_limiter"))
        return ApplyResult::Unsupported;
    auto reset = reinterpret_cast<LPALCRESETDEVICESOFT>(
        alcGetProcAddress(device, "alcResetDeviceSOFT"));
    if (!reset)
        return ApplyResult::Unsupported;
    if (reset(device, attrs.data()))
        return ApplyResult::Applied;

    // A failed reset has weaker guarantees than a failed reopen. Try restoring
    // the previous attributes while keeping all existing AL objects alive.
    const auto oldAttrs = Attributes(device, previous);
    reset(device, oldAttrs.data());
    return ApplyResult::Failed;
}

inline const char* OutputName(ALCint mode) {
    switch (mode) {
    case ALC_MONO_SOFT: return "Mono";
    case ALC_STEREO_SOFT: return "Stereo";
    case ALC_STEREO_BASIC_SOFT: return "Stereo";
    case ALC_STEREO_HRTF_SOFT: return "Stereo (HRTF)";
    case ALC_STEREO_UHJ_SOFT: return "Stereo (UHJ)";
    case ALC_QUAD_SOFT: return "Quad";
    case ALC_SURROUND_5_1_SOFT: return "5.1";
    case ALC_SURROUND_6_1_SOFT: return "6.1";
    case ALC_SURROUND_7_1_SOFT: return "7.1";
    default: return "Auto";
    }
}
} // namespace SoundDevice
