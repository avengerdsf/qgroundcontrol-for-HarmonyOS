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
    void rtpH264Loopback();
    void rtspTransportNegotiation_data();
    void rtspTransportNegotiation();
#ifndef QGC_FFMPEG_NATIVE_TEST
    void disabledStreamDoesNotRestart();
    void explicitStopDoesNotRestart();
    void disabledFactStopsVideo();
    void sourceDisabledThenEnabledStarts();
#endif

private:
#ifndef QGC_FFMPEG_NATIVE_TEST
    void _verifyManagerStop(bool disableStream, bool explicitStop = true);
#endif
};
