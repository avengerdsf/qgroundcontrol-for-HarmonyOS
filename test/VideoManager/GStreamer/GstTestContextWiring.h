#pragma once

/// Installs the same QGC↔GStreamer context wiring the production receiver uses, for tests that
/// build raw pipelines with gst_parse_launch instead of going through GstVideoReceiver.
///
/// Production installs HwBuffers::onBusSyncMessage on the pipeline bus (GstVideoReceiver.cc) and
/// qgcvideosinkbin intercepts context queries on its internal sink pad. A raw test pipeline without
/// both gets no GstGLDisplay/GstGLContext from the Qt bridge, so glupload falls back to creating its
/// own context and fails with "glGetString not defined or returned invalid value".

#include <gst/gst.h>

#include "HwBuffers.h"

namespace GstTestContextWiring {

/// Answer NEED_CONTEXT (GL display / app context, Vulkan, D3D) from the Qt-side bridges.
inline void installBusHandler(GstElement* pipeline)
{
#if defined(QGC_HAS_ANY_GPU_PATH)
    if (!pipeline) {
        return;
    }
    if (GstBus* bus = gst_element_get_bus(pipeline)) {
        gst_bus_set_sync_handler(bus, HwBuffers::onBusSyncMessage, nullptr, nullptr);
        gst_object_unref(bus);
    }
#else
    Q_UNUSED(pipeline);
#endif
}

inline GstPadProbeReturn answerContextQuery(GstPad*, GstPadProbeInfo* info, gpointer)
{
#if defined(QGC_HAS_ANY_GPU_PATH)
    if (GstQuery* query = GST_PAD_PROBE_INFO_QUERY(info)) {
        if (HwBuffers::answerSinkBinContextQuery(query)) {
            return GST_PAD_PROBE_HANDLED;
        }
    }
#else
    Q_UNUSED(info);
#endif
    return GST_PAD_PROBE_OK;
}

/// Terminate downstream context queries at @p sink (the sink-most element of a raw pipeline),
/// mirroring the probe qgcvideosinkbin installs on its videosink pad.
inline gulong installSinkQueryProbe(GstElement* sink)
{
#if defined(QGC_HAS_ANY_GPU_PATH)
    GstPad* pad = sink ? gst_element_get_static_pad(sink, "sink") : nullptr;
    if (!pad) {
        return 0;
    }
    const gulong probeId = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, &answerContextQuery,
                                            nullptr, nullptr);
    gst_object_unref(pad);
    return probeId;
#else
    Q_UNUSED(sink);
    return 0;
#endif
}

}  // namespace GstTestContextWiring
