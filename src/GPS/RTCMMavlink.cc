/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "RTCMMavlink.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(RTCMMavlinkLog, "qgc.gps.rtcmmavlink")

RTCMMavlink::RTCMMavlink(QObject *parent)
    : QObject(parent)
{
    // qCDebug(RTCMMavlinkLog) << Q_FUNC_INFO << this;

    _bandwidthTimer.start();
}

RTCMMavlink::~RTCMMavlink()
{
    // qCDebug(RTCMMavlinkLog) << Q_FUNC_INFO << this;
}

void RTCMMavlink::RTCMDataUpdate(QByteArrayView data)
{
    if (data.isEmpty()) { return; }
#ifdef QT_DEBUG
    _calculateBandwith(data.size());
#endif

    static constexpr qsizetype maxMessageLength = MAVLINK_MSG_GPS_RTCM_DATA_FIELD_DATA_LEN;
    for (qsizetype start = 0; start < data.size();) {
        const qsizetype bundleLength = std::min(data.size() - start, maxMessageLength * 4);
        const bool fragmented = bundleLength > maxMessageLength;
        // A full last fragment needs a zero-length terminator unless all four are present.
        const qsizetype count = fragmented ? std::min<qsizetype>(4, bundleLength / maxMessageLength + 1) : 1;
        for (qsizetype fragment = 0; fragment < count; ++fragment) {
            mavlink_gps_rtcm_data_t message{};
            const qsizetype length = std::min(bundleLength - fragment * maxMessageLength, maxMessageLength);
            message.flags = (_sequenceId & 0x1FU) << 3;
            if (fragmented) { message.flags |= 0x01U | (fragment << 1); }
            message.len = length;
            if (length) { (void) memcpy(message.data, data.constData() + start + fragment * maxMessageLength, length); }
            _sendMessageToVehicle(message);
        }
        start += bundleLength;
        ++_sequenceId;
    }
}

void RTCMMavlink::_sendMessageToVehicle(const mavlink_gps_rtcm_data_t &data)
{
    QmlObjectListModel* const vehicles = MultiVehicleManager::instance()->vehicles();
    for (qsizetype i = 0; i < vehicles->count(); i++) {
        Vehicle* const vehicle = qobject_cast<Vehicle*>(vehicles->get(i));
        const SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (sharedLink) {
            mavlink_message_t message;
            (void) mavlink_msg_gps_rtcm_data_encode_chan(
                MAVLinkProtocol::instance()->getSystemId(),
                MAVLinkProtocol::getComponentId(),
                sharedLink->mavlinkChannel(),
                &message,
                &data
            );
            (void) vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), message);
        }
    }
}

void RTCMMavlink::_calculateBandwith(qsizetype bytes)
{
    if (!_bandwidthTimer.isValid()) {
        return;
    }

    _bandwidthByteCounter += bytes;

    const qint64 elapsed = _bandwidthTimer.elapsed();
    if (elapsed > 1000) {
        qCDebug(RTCMMavlinkLog) << QStringLiteral("RTCM bandwidth: %1 kB/s").arg(((_bandwidthByteCounter / elapsed) * 1000.f) / 1024.f);
        (void) _bandwidthTimer.restart();
        _bandwidthByteCounter = 0;
    }
}
