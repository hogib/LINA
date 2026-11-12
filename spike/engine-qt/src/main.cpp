// LINA feasibility spike: Tracktion Engine (JUCE) + Qt Quick in one process.
//
//   lina_spike                               windowed app, real audio device
//   lina_spike --audio null                  windowed app, no audio device opened
//   lina_spike --plugin-path /path/x.vst3     ... plus a plugin on track 1 with its editor window
//   lina_spike --render out.wav              offline render + sanity check, no window, no device
//   lina_spike --selftest [--loop poll]      headless checks of the JUCE/Qt seam

#include "EditModels.h"
#include "EngineHost.h"
#include "JuceQtEventLoop.h"
#include "SelfTest.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace
{
/** Reads the rendered file back and checks length and level. Returns true if it looks right. */
bool verifyRender (const juce::File& file, double expectedSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        std::printf ("FAIL  render: cannot read %s\n", file.getFullPathName().toRawUTF8());
        return false;
    }

    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), numSamples);
    reader->read (&buffer, 0, numSamples, 0, true, true);

    const double seconds = numSamples / reader->sampleRate;
    const float peak = buffer.getMagnitude (0, numSamples);
    float rms = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        rms = std::max (rms, buffer.getRMSLevel (ch, 0, numSamples));

    const bool lengthOk = std::abs (seconds - expectedSeconds) < 0.05;
    const bool levelOk = peak > 0.05f && peak <= 1.0f && rms > 0.01f;

    std::printf ("render: %s\n", file.getFullPathName().toRawUTF8());
    std::printf ("render: %d ch, %.0f Hz, %u bit, %d samples = %.4f s (expected %.4f s)\n",
                 static_cast<int> (reader->numChannels), reader->sampleRate, reader->bitsPerSample,
                 numSamples, seconds, expectedSeconds);
    std::printf ("render: peak %.4f (%.1f dBFS), rms %.4f (%.1f dBFS)\n",
                 peak, juce::Decibels::gainToDecibels (peak), rms, juce::Decibels::gainToDecibels (rms));
    std::printf ("%s  render length\n%s  render is non-silent\n", lengthOk ? "PASS" : "FAIL", levelOk ? "PASS" : "FAIL");
    return lengthOk && levelOk;
}
} // namespace

int main (int argc, char** argv)
{
    // Decide headless-ness before either toolkit initialises.
    bool selftest = false, render = false;

    for (int i = 1; i < argc; ++i)
    {
        selftest = selftest || qstrcmp (argv[i], "--selftest") == 0;
        render = render || qstrcmp (argv[i], "--render") == 0;
    }

    if (selftest || render)
    {
        qputenv ("QT_QPA_PLATFORM", "offscreen"); // no Qt window can reach a real display
        ::unsetenv ("DISPLAY");                   // JUCE is X11-only: no display, no JUCE window either
    }

    if (qEnvironmentVariableIsEmpty ("QT_QUICK_CONTROLS_STYLE"))
        qputenv ("QT_QUICK_CONTROLS_STYLE", "Basic");

    QGuiApplication app (argc, argv);
    QCoreApplication::setApplicationName ("lina-spike");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption renderOpt ("render", "Render the edit offline to <file> and check it.", "file");
    QCommandLineOption selftestOpt ("selftest", "Run the headless seam checks.");
    // Not "--plugin": QGuiApplication swallows "-plugin <x>" / "--plugin <x>" as its own option.
    QCommandLineOption pluginOpt ("plugin-path", "VST3/LV2 plugin to scan and load on track 1.", "path");
    QCommandLineOption audioOpt ("audio", "Windowed mode audio: real (default) or null.", "mode", "real");
    QCommandLineOption loopOpt ("loop", "Event loop glue: fd (default) or poll.", "mode", "fd");
    QCommandLineOption stateOpt ("state-dir", "Where Tracktion keeps settings/temp files.", "dir",
                                 QDir (QCoreApplication::applicationDirPath()).filePath ("state"));
    parser.addOptions ({ renderOpt, selftestOpt, pluginOpt, audioOpt, loopOpt, stateOpt });
    parser.process (app);

    const auto pluginPath = parser.value (pluginOpt);
    const juce::File stateDir (toJuceString (QDir (parser.value (stateOpt)).absolutePath()));
    const auto loopMode = parser.value (loopOpt) == "poll" ? JuceQtEventLoop::Mode::PollTimer
                                                           : JuceQtEventLoop::Mode::FdNotifiers;

    // Creates juce::MessageManager on this thread, which makes the Qt main thread JUCE's
    // message thread. JUCE's own dispatch loop (runDispatchLoop) is never started.
    juce::ScopedJuceInitialiser_GUI juceInit;
    int exitCode = 0;

    {
        JuceQtEventLoop loop (loopMode);

        //====================================================================== --render
        if (render)
        {
            EngineHost host (AudioMode::none, stateDir);
            host.logger().echo = false;
            const juce::File out (toJuceString (QDir::current().absoluteFilePath (parser.value (renderOpt))));

            if (! pluginPath.isEmpty())
            {
                juce::String error;

                if (host.loadExternalPlugin (toJuceString (pluginPath), 0, error) == nullptr)
                    std::printf ("render: plugin not loaded: %s\n", error.toRawUTF8());
            }

            const double expected = host.edit().getLength().inSeconds();
            const bool rendered = host.renderToFile (out);
            std::printf ("%s  renderer produced a file\n", rendered ? "PASS" : "FAIL");
            const bool ok = rendered && verifyRender (out, expected);
            const int assertions = host.logger().numAssertions.load();
            std::printf ("render: %d JUCE assertion(s) logged\n", assertions);
            exitCode = ok ? 0 : 1;
        }
        //====================================================================== --selftest / windowed
        else
        {
            const auto audio = selftest || parser.value (audioOpt) == "null" ? AudioMode::null : AudioMode::real;
            EngineHost host (audio, stateDir);
            host.logger().echo = ! selftest || qEnvironmentVariableIsSet ("LINA_SPIKE_VERBOSE");
            EditController controller (host.edit());

            QQmlApplicationEngine qml;
            qml.setInitialProperties ({ { "controller", QVariant::fromValue (&controller) } });
            qml.loadFromModule ("Lina.Spike", "Main");
            QObject* root = qml.rootObjects().isEmpty() ? nullptr : qml.rootObjects().constFirst();

            if (selftest)
            {
                SelfTestContext ctx { loop, host, controller, root, pluginPath };
                QTimer::singleShot (0, &app, [&] { QCoreApplication::exit (runSelfTest (ctx) == 0 ? 0 : 1); });
                exitCode = app.exec();
            }
            else if (root == nullptr)
            {
                std::fprintf (stderr, "Main.qml failed to load\n");
                exitCode = 2;
            }
            else
            {
                if (! pluginPath.isEmpty())
                {
                    // Deferred so the Qt window is up first; the editor is a second, JUCE-owned
                    // top-level window (X11 / XWayland).
                    QTimer::singleShot (0, &app, [&]
                    {
                        juce::String error;

                        if (auto plugin = host.loadExternalPlugin (toJuceString (pluginPath), 0, error))
                            host.showPluginEditor (*plugin);
                        else
                            std::fprintf (stderr, "plugin not loaded: %s\n", error.toRawUTF8());
                    });
                }

                exitCode = app.exec();
            }
        }
    }

    return exitCode;
}
