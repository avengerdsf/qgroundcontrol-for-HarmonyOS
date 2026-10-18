#pragma once
#include <QtQuick/QQuickWindow>

class QQuickWindow;

class QGCApplication
{
public:
    QQuickWindow* rootWindow = nullptr;
    bool unitTesting = false;

    static void processEvents() { QCoreApplication::processEvents(); }

    bool runningUnitTests() const { return unitTesting; }

    QQuickWindow* mainRootWindow() const { return rootWindow; }
};

inline QGCApplication* audioStartupTestApplication = nullptr;

inline QGCApplication* qgcApp()
{
    return audioStartupTestApplication;
}

#ifdef qApp
#undef qApp
#endif
#define qApp (audioStartupTestApplication)
