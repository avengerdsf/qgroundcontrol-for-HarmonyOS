#pragma once
#include <QtCore/QStringList>

class CalibrationTestApplication
{
public:
    void showAppMessage(const QString &message) { messages.append(message); }
    QStringList messages;
};
inline CalibrationTestApplication *qgcApp()
{
    static CalibrationTestApplication application;
    return &application;
}
