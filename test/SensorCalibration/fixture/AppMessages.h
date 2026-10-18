#pragma once
#include "QGCApplication.h"

namespace QGC {
inline void showAppMessage(const QString& message, const QString& = QString())
{
    qgcApp()->showAppMessage(message);
}
}  // namespace QGC
