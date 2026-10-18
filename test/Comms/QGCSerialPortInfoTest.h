#pragma once

#include "UnitTest.h"

class QGCSerialPortInfoTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testHarmonyEnumerationErrors();
    void _testHarmonyCubeBlack();
    void _testLoadJsonData();
    void _testLoadJsonDataIdempotent();
    void _testBoardClassStringToType();
    void _testBoardTypeToString();
    void _testBoardClassStringToTypeCaseInsensitivity();
    void _testBoardTypeStringRoundTrip();
    void _testBoardInfoListEntriesAreWellFormed();
    void _testFallbackRegexesCompile();
};
