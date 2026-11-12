#include "EngineHost.h"

#include <cmath>
#include <numbers>

using namespace tracktion::literals;

//==============================================================================
void CountingLogger::logMessage (const juce::String& message)
{
    if (message.contains ("JUCE Assertion failure"))
        ++numAssertions;

    if (echo)
        std::fprintf (stderr, "[juce] %s\n", message.toRawUTF8());
}

//==============================================================================
namespace
{
struct SpikePropertyStorage final : te::PropertyStorage
{
    SpikePropertyStorage (const juce::File& d) : te::PropertyStorage ("lina-spike"), dir (d) {}

    juce::File getAppCacheFolder() override   { return getAppPrefsFolder(); }
    juce::File getAppPrefsFolder() override   { dir.createDirectory(); return dir; }

    juce::File dir;
};

struct SpikeEngineBehaviour final : te::EngineBehaviour
{
    explicit SpikeEngineBehaviour (AudioMode m) : mode (m) {}

    // We decide when (and whether) a device is opened; the Engine constructor must not.
    bool autoInitialiseDeviceManager() override     { return false; }
    bool addSystemAudioIODeviceTypes() override     { return mode == AudioMode::real; }
    bool shouldOpenAudioInputByDefault() override   { return false; }

    AudioMode mode;
};

//==============================================================================
/** The JUCE top-level window that hosts a plugin editor. This is the only juce::Component
    peer the application ever creates. */
struct PluginEditorWindow final : juce::DocumentWindow
{
    explicit PluginEditorWindow (te::Plugin& p)
        : juce::DocumentWindow (p.getName(), juce::Colours::black, juce::DocumentWindow::closeButton, true),
          plugin (p)
    {
        setUsingNativeTitleBar (true);

        if (auto editor = plugin.createEditor())
        {
            const bool resizable = editor->allowWindowResizing();
            setContentOwned (editor.release(), true);
            setResizable (resizable, false);
        }

        centreWithSize (getWidth(), getHeight());
    }

    void closeButtonPressed() override   { plugin.windowState->closeWindowExplicitly(); }

    bool hasEditor() const               { return getContentComponent() != nullptr; }

    te::Plugin& plugin;
};

struct SpikeUIBehaviour final : te::UIBehaviour
{
    // The default implementations of the alert functions open juce::AlertWindows. The
    // application UI is Qt, so they are reduced to log lines here.
    void showWarningAlert (const juce::String& title, const juce::String& message) override
    {
        juce::Logger::writeToLog ("WARNING ALERT: " + title + ": " + message);
    }

    bool showOkCancelAlertBox (const juce::String& title, const juce::String& message,
                               const juce::String&, const juce::String&) override
    {
        juce::Logger::writeToLog ("OK/CANCEL (auto-OK): " + title + ": " + message);
        return true;
    }

    int showYesNoCancelAlertBox (const juce::String& title, const juce::String& message,
                                 const juce::String&, const juce::String&, const juce::String&) override
    {
        juce::Logger::writeToLog ("YES/NO/CANCEL (auto-yes): " + title + ": " + message);
        return 1;
    }

    void showInfoMessage (const juce::String& message) override      { juce::Logger::writeToLog ("INFO: " + message); }
    void showWarningMessage (const juce::String& message) override   { juce::Logger::writeToLog ("WARNING: " + message); }

    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain) {}
    }

    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
    {
        if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&state))
        {
            auto w = std::make_unique<PluginEditorWindow> (ws->plugin);

            if (w->hasEditor())
                return w;

            juce::Logger::writeToLog ("Plugin has no editor: " + ws->plugin.getName());
        }

        return {};
    }
};
} // namespace

//==============================================================================
/** Stands in for an audio device: calls the engine's audio callback at roughly real-time
    rate and throws the output away. Uses Tracktion's "engine inside a plugin" interface. */
struct EngineHost::NullAudioPump final : juce::Thread
{
    explicit NullAudioPump (te::Engine& e)
        : juce::Thread ("null audio pump"),
          iface (e.getDeviceManager().getHostedAudioDeviceInterface())
    {
        te::HostedAudioDeviceInterface::Parameters p;
        p.sampleRate = nullDeviceSampleRate;
        p.blockSize = nullDeviceBlockSize;
        p.inputChannels = 0;
        p.outputChannels = 2;
        p.useMidiDevices = false;
        iface.initialise (p);
        iface.prepareToPlay (p.sampleRate, p.blockSize);
        startThread();
    }

    ~NullAudioPump() override   { stopThread (2000); }

    void run() override
    {
        juce::AudioBuffer<float> buffer (2, nullDeviceBlockSize);
        juce::MidiBuffer midi;
        const double blockMs = 1000.0 * nullDeviceBlockSize / nullDeviceSampleRate;
        double next = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            buffer.clear();
            midi.clear();
            iface.processBlock (buffer, midi);

            next += blockMs;
            const auto waitMs = static_cast<int> (next - juce::Time::getMillisecondCounterHiRes());

            if (waitMs > 0)
                wait (waitMs);
        }
    }

    te::HostedAudioDeviceInterface& iface;
};

//==============================================================================
EngineHost::EngineHost (AudioMode mode, const juce::File& dir)
    : stateDir (dir)
{
    juce::Logger::setCurrentLogger (&log);
    stateDir.createDirectory();

    eng = std::make_unique<te::Engine> (std::make_unique<SpikePropertyStorage> (stateDir),
                                        std::make_unique<SpikeUIBehaviour>(),
                                        std::make_unique<SpikeEngineBehaviour> (mode));

    if (mode == AudioMode::null)
        pump = std::make_unique<NullAudioPump> (*eng);
    else if (mode == AudioMode::real)
        eng->getDeviceManager().initialise (0, 2);

    buildEdit();
}

EngineHost::~EngineHost()
{
    if (ed != nullptr)
    {
        ed->getTransport().stop (false, true);

        for (auto* p : te::getAllPlugins (*ed, false))
            p->hideWindowForShutdown();
    }

    pump.reset();
    ed.reset();
    eng.reset();
    juce::Logger::setCurrentLogger (nullptr);
}

te::AudioTrack* EngineHost::audioTrack (int index) const
{
    return te::getAudioTracks (*ed)[index];
}

bool EngineHost::writeSweep (const juce::File& file, double seconds, double sampleRate)
{
    file.deleteFile();
    const int numSamples = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (1, numSamples);

    // Exponential sine sweep 110 Hz -> 1760 Hz, 10 ms fades, -6 dBFS.
    const double f0 = 110.0, f1 = 1760.0;
    const double k = std::log (f1 / f0);
    const int fade = static_cast<int> (0.01 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        const double phase = 2.0 * std::numbers::pi * f0 * seconds / k * (std::exp (k * t / seconds) - 1.0);
        const double env = std::min ({ 1.0, i / double (fade), (numSamples - 1 - i) / double (fade) });
        buffer.setSample (0, i, static_cast<float> (0.5 * env * std::sin (phase)));
    }

    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();

    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, 1, 16, {}, 0));

    if (writer == nullptr)
        return false;

    stream.release(); // the writer owns it now
    return writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
}

void EngineHost::buildEdit()
{
    sweep = stateDir.getChildFile ("sweep.wav");

    if (! writeSweep (sweep, sweepSeconds, 44100.0))
        juce::Logger::writeToLog ("Could not write " + sweep.getFullPathName());

    te::Edit::Options options { *eng, te::createEmptyEdit (*eng), te::ProjectItemID::createNewID (0) };
    options.numAudioTracks = 2;
    ed = te::Edit::createEdit (options);

    auto* audio = audioTrack (0);
    auto* midi = audioTrack (1);
    audio->setName ("Audio");
    midi->setName ("MIDI");

    const te::TimeRange fourSeconds { 0_tp, te::TimeDuration::fromSeconds (sweepSeconds) };

    te::insertWaveClip (*audio, "Sweep", sweep, te::ClipPosition { fourSeconds }, te::DeleteExistingClips::no);

    if (auto clip = te::insertMIDIClip (*midi, "Arp", fourSeconds))
    {
        auto& seq = clip->getSequence();
        const int pitches[] = { 57, 60, 64, 69, 64, 60, 57, 52 };

        for (int i = 0; i < 8; ++i)
            seq.addNote (pitches[i], te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.9), 100, 0, nullptr);
    }

    // A built-in synth so the MIDI clip is audible in the render.
    if (auto synth = ed->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
        midi->pluginList.insertPlugin (synth, 0, nullptr);

    ed->getUndoManager().clearUndoHistory();
    ed->getTransport().ensureContextAllocated();
}

bool EngineHost::renderToFile (const juce::File& destination)
{
    destination.deleteFile();

    // useThread = false: the render task runs to completion on this thread.
    return te::Renderer::renderToFile (*ed, destination, false);
}

te::Plugin::Ptr EngineHost::loadExternalPlugin (const juce::String& path, int trackIndex, juce::String& error)
{
    auto& pm = eng->getPluginManager();
    juce::OwnedArray<juce::PluginDescription> found;

    // In-process scan: a plugin that crashes while being scanned takes the app with it.
    // A real application wants Tracktion's out-of-process scanner (canScanPluginsOutOfProcess).
    for (auto* format : pm.pluginFormatManager.getFormats())
    {
        if (! format->fileMightContainThisPluginType (path))
            continue;

        juce::Logger::writeToLog ("Scanning " + path + " as " + format->getName());
        pm.knownPluginList.scanAndAddFile (path, true, found, *format);

        if (! found.isEmpty())
            break;
    }

    if (found.isEmpty())
    {
        error = "no plugin found at " + path + " (formats built in:";

        for (auto* format : pm.pluginFormatManager.getFormats())
            error << " " << format->getName();

        error << ")";
        return {};
    }

    const auto& desc = *found.getFirst();
    juce::Logger::writeToLog ("Found: " + desc.name + " / " + desc.manufacturerName + " / "
                              + desc.pluginFormatName + " / " + desc.version
                              + " ins=" + juce::String (desc.numInputChannels)
                              + " outs=" + juce::String (desc.numOutputChannels));

    auto plugin = ed->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

    if (plugin == nullptr)
    {
        error = "Tracktion could not create a plugin object for " + desc.name;
        return {};
    }

    audioTrack (trackIndex)->pluginList.insertPlugin (plugin, 0, nullptr);

    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
    {
        if (! ext->isInitialisingAsync() && ext->getAudioPluginInstance() == nullptr)
        {
            error = "plugin instance was not created: " + ext->getLoadError();
            return {};
        }
    }

    return plugin;
}

void EngineHost::showPluginEditor (te::Plugin& plugin)
{
    // Goes through PluginWindowState -> UIBehaviour::createPluginWindow above.
    plugin.showWindowExplicitly();
}
