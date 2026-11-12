#pragma once

// Engine setup: Tracktion Engine, one Edit built in code, offline render, plugin loading and
// the JUCE-side plugin editor window. No Qt in here.

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <memory>

namespace te = tracktion;

enum class AudioMode
{
    none,   // no audio device at all (offline render)
    null,   // Tracktion's HostedAudioDeviceInterface fed by a pump thread; nothing is opened
    real    // the system default output device (ALSA)
};

/** Counts "JUCE Assertion failure" lines (JUCE_LOG_ASSERTIONS=1 routes them here). */
struct CountingLogger final : juce::Logger
{
    void logMessage (const juce::String& message) override;
    std::atomic<int> numAssertions { 0 };
    bool echo = true;
};

class EngineHost final
{
public:
    /** stateDir receives Tracktion's settings and temp files, so nothing lands in ~/.config. */
    EngineHost (AudioMode, const juce::File& stateDir);
    ~EngineHost();

    te::Engine& engine() noexcept       { return *eng; }
    te::Edit& edit() noexcept           { return *ed; }
    CountingLogger& logger() noexcept   { return log; }

    te::AudioTrack* audioTrack (int index) const;
    juce::File sweepFile() const        { return sweep; }

    /** Offline render of the whole edit on the calling thread. */
    bool renderToFile (const juce::File& destination);

    /** Scans one plugin file/bundle in-process with every registered format, inserts the
        first plugin found on the given track. Returns nullptr and fills `error` on failure. */
    te::Plugin::Ptr loadExternalPlugin (const juce::String& path, int trackIndex, juce::String& error);

    /** Opens the plugin's editor in a JUCE top-level window (X11). */
    void showPluginEditor (te::Plugin&);

    static constexpr double sweepSeconds = 4.0;
    static constexpr double nullDeviceSampleRate = 44100.0;
    static constexpr int nullDeviceBlockSize = 512;

private:
    struct NullAudioPump;

    void buildEdit();
    static bool writeSweep (const juce::File&, double seconds, double sampleRate);

    CountingLogger log;
    juce::File stateDir, sweep;
    std::unique_ptr<te::Engine> eng;
    std::unique_ptr<te::Edit> ed;
    std::unique_ptr<NullAudioPump> pump;
};
