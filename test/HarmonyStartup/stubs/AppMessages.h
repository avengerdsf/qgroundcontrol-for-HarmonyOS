#pragma once
#include "QGCApplication.h"

namespace QGC {
inline bool runningUnitTests()
{
    return audioStartupTestApplication && audioStartupTestApplication->runningUnitTests();
}
}  // namespace QGC
