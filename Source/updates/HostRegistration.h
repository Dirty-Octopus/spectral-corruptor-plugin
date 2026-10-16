// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
 #include <process.h>
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_MAC
 #include <sys/sysctl.h>
 #include <unistd.h>
#else
 #include <unistd.h>
#endif
namespace scrr::updates {
inline int processId()
{
   #if JUCE_WINDOWS
    return _getpid();
   #else
    return (int) getpid();
   #endif
}
// Pair the PID with the OS process creation time. A later unrelated process
// reusing this PID must never be mistaken for the original audio host.
inline juce::String processStartIdentity()
{
   #if JUCE_WINDOWS
    FILETIME created {}, exited {}, kernel {}, user {};
    if (! GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user)) return {};
    const auto value = (static_cast<juce::uint64> (created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return juce::String (static_cast<juce::int64> (value));
   #elif JUCE_MAC
    struct kinfo_proc info {};
    size_t size = sizeof (info);
    int mib[] { CTL_KERN, KERN_PROC, KERN_PROC_PID, processId() };
    if (sysctl (mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) return {};
    return juce::String (static_cast<juce::int64> (info.kp_proc.p_starttime.tv_sec));
   #else
    return {};
   #endif
}
inline juce::File dataDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
   #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
    return base.getChildFile ("SpectralCorruptorSource/Updates");
   #else
    return base.getChildFile ("SpectralCorruptor/Updates");
   #endif
}
class HostRegistration
{
public:
    HostRegistration()
    {
       #if ! defined(SCR_SOURCE_EDITION) || ! SCR_SOURCE_EDITION
        const auto marker = dataDirectory().getChildFile ("hosts").getChildFile (juce::String (processId()) + ".pid");
        const auto identity = processStartIdentity();
        if (identity.isNotEmpty() && marker.getParentDirectory().createDirectory().wasOk())
        {
            juce::TemporaryFile temporary (marker);
            if (temporary.getFile().replaceWithText (juce::String (processId()) + " " + identity))
                temporary.overwriteTargetFileWithTemporary();
        }
       #endif
    }
    // Keep the process marker when a plugin/editor is removed. Only the helper's
    // PID + creation-time check determines that the host has actually exited.
};
}
