#pragma once

#include "UnitTest.h"

/// Tests for Platform utilities (safety checks, environment setup)
class PlatformTest : public UnitTest
{
    Q_OBJECT

private slots:
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(Q_OS_HARMONY)
    // Single instance guard tests
    void _testCheckSingleInstanceAllowMultiple();
    void _testCheckSingleInstanceBlocks();
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && !defined(Q_OS_HARMONY)
    // Root detection tests (Linux only)
    void _testIsRunningAsRootNormal();
#endif

#if defined(QGC_UNITTEST_BUILD)
    // Unit-test initialization environment setup
    void _testInitializeSetsUnitTestEnvironment();
#endif
};
