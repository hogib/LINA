#pragma once

#include <QString>

class EditController;
class EngineHost;
class JuceQtEventLoop;
class QObject;

struct SelfTestContext
{
    JuceQtEventLoop& loop;
    EngineHost& host;
    EditController& controller;
    QObject* qmlRoot = nullptr;   // the ApplicationWindow, or nullptr if QML failed to load
    QString pluginPath;           // optional: scan + instantiate (the editor is NOT opened)
};

/** Runs inside QCoreApplication::exec(). Prints PASS/FAIL per check; returns the number of FAILs. */
int runSelfTest (SelfTestContext&);
