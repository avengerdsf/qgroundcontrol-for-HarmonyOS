#pragma once

#ifdef QGC_FFMPEG_NATIVE_TEST
#include <QtCore/QObject>
using FFmpegVideoTestBase = QObject;
#else
#include "UnitTest.h"
using FFmpegVideoTestBase = UnitTest;
#endif

class FFmpegVideoReceiverTest : public FFmpegVideoTestBase
{
    Q_OBJECT
private slots:
    void invalidUri();
    void cancelPendingStream();
    void decodeRecordScreenshot();
    void destroyedSinkAllowsRebinding();
    void rtpH264Loopback();
    void rtspTransportNegotiation_data();
    void rtspTransportNegotiation();
#ifndef QGC_FFMPEG_NATIVE_TEST
    void badMediaFailsToStart();
    void disabledStreamDoesNotRestart();
    void explicitStopDoesNotRestart();
    void disabledFactStopsVideo();
    void sourceDisabledThenEnabledStarts();
    void autoReconnectPolicyAfterTransportLoss_data();
    void autoReconnectPolicyAfterTransportLoss();
    void autoReconnectDisabledAllowsExplicitRestart_data();
    void autoReconnectDisabledAllowsExplicitRestart();
#endif

private:
#ifndef QGC_FFMPEG_NATIVE_TEST
    void _verifyManagerStop(bool disableStream, bool explicitStop = true);
#endif
};
