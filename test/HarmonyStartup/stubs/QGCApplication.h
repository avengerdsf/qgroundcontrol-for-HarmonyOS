#pragma once

class QQuickWindow;

class QGCApplication
{
public:
    QQuickWindow *rootWindow = nullptr;
    bool unitTesting = false;
    bool runningUnitTests() const { return unitTesting; }
    QQuickWindow *mainRootWindow() const { return rootWindow; }
};

inline QGCApplication *audioStartupTestApplication = nullptr;
inline QGCApplication *qgcApp() { return audioStartupTestApplication; }
