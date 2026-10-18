#include "GPSRtk.h"

#include <QtCore/QPointer>
#include <utility>

#include "GPSProvider.h"
#include "GPSRTKFactGroup.h"
#include "GPSType.h"
#include "NTRIPManager.h"
#include "QGCLoggingCategory.h"
#include "RTCMMavlink.h"
#include "RTKSettings.h"
#include "SettingsManager.h"

QGC_LOGGING_CATEGORY(GPSRtkLog, "GPS.GPSRtk")

namespace {
struct GPSTypeEntry
{
    QLatin1StringView key;
    GPSType type;
    int manufacturerId;  // RTKSettings::baseReceiverManufacturers enum value
};

constexpr GPSTypeEntry kGPSTypeTable[] = {
    {QLatin1StringView("trimble"), GPSType::trimble, 1},
    {QLatin1StringView("septentrio"), GPSType::septentrio, 2},
    {QLatin1StringView("femtomes"), GPSType::femto, 3},
    {QLatin1StringView("blox"), GPSType::u_blox, 4},
};
}  // namespace

GPSRtk::GPSRtk(QObject* parent) : QObject(parent), _gpsRtkFactGroup(new GPSRTKFactGroup(this))
{
    qCDebug(GPSRtkLog) << this;

    (void) qRegisterMetaType<satellite_info_s>("satellite_info_s");
    (void) qRegisterMetaType<sensor_gps_s>("sensor_gps_s");
    (void) qRegisterMetaType<GPSConnectionError>("GPSConnectionError");
    (void) qRegisterMetaType<GPSSurveyInStatus>("GPSSurveyInStatus");
}

GPSRtk::~GPSRtk()
{
    disconnectGPS();

    qCDebug(GPSRtkLog) << this;
}

void GPSRtk::_onGPSConnect()
{
    _gpsRtkFactGroup->connected()->setRawValue(true);
}

void GPSRtk::_onGPSDisconnect()
{
    _gpsRtkFactGroup->connected()->setRawValue(false);
}

void GPSRtk::_onGPSConnectionError(GPSConnectionError error)
{
    switch (error) {
        case GPSConnectionError::OpenFailed:
            qCWarning(GPSRtkLog) << "Failed to open GPS serial device";
            break;
        case GPSConnectionError::ConfigFailed:
            qCWarning(GPSRtkLog) << "GPS receiver did not accept configuration";
            break;
        case GPSConnectionError::DeviceError:
            qCWarning(GPSRtkLog) << "GPS device error, connection lost";
            break;
        case GPSConnectionError::None:
            break;
    }

    _gpsRtkFactGroup->lastError()->setRawValue(static_cast<int>(error));
}

void GPSRtk::_onGPSSurveyInStatus(const GPSSurveyInStatus& status)
{
    _gpsRtkFactGroup->currentDuration()->setRawValue(status.durationSecs);
    _gpsRtkFactGroup->currentAccuracy()->setRawValue(static_cast<double>(status.meanAccuracyMM) / 1000.0);
    _gpsRtkFactGroup->currentLatitude()->setRawValue(status.latitude);
    _gpsRtkFactGroup->currentLongitude()->setRawValue(status.longitude);
    _gpsRtkFactGroup->currentAltitude()->setRawValue(status.altitude);
    _gpsRtkFactGroup->valid()->setRawValue(status.valid);
    _gpsRtkFactGroup->active()->setRawValue(status.active);
}

void GPSRtk::connectGPS(const QString& device, QStringView gps_type)
{
    RTKSettings* const rtkSettings = SettingsManager::instance()->rtkSettings();

    GPSType type = GPSType::u_blox;
    int manufacturerId = 4;  // u-blox by default
    for (const GPSTypeEntry& entry : kGPSTypeTable) {
        if (gps_type.contains(entry.key, Qt::CaseInsensitive)) {
            type = entry.type;
            manufacturerId = entry.manufacturerId;
            break;
        }
    }
    rtkSettings->baseReceiverManufacturers()->setRawValue(manufacturerId);
    qCDebug(GPSRtkLog) << "Connecting GPS device" << gps_type << "manufacturer id" << manufacturerId;

    disconnectGPS();

#ifdef Q_OS_HARMONY
    _requestGpsStop = std::make_shared<std::atomic_bool>(false);
#else
    _requestGpsStop = false;
#endif
    _gpsRtkFactGroup->lastError()->setRawValue(static_cast<int>(GPSConnectionError::None));
    const bool useFixedBase =
        static_cast<BaseModeDefinition::Mode>(rtkSettings->useFixedBasePosition()->rawValue().toInt()) ==
        BaseModeDefinition::Mode::BaseFixed;
    const GPSReceiverConfig rtkConfig = {
        .useFixedBase = useFixedBase,
        .surveyInAccMeters = rtkSettings->surveyInAccuracyLimit()->rawValue().toDouble(),
        .surveyInDurationSecs = rtkSettings->surveyInMinObservationDuration()->rawValue().toInt(),
        .fixedBaseLatitude = rtkSettings->fixedBasePositionLatitude()->rawValue().toDouble(),
        .fixedBaseLongitude = rtkSettings->fixedBasePositionLongitude()->rawValue().toDouble(),
        .fixedBaseAltitudeMeters = rtkSettings->fixedBasePositionAltitude()->rawValue().toFloat(),
        .fixedBaseAccuracyMeters = rtkSettings->fixedBasePositionAccuracy()->rawValue().toFloat(),
    };
    _gpsProvider = new GPSProvider(device, type, rtkConfig, _requestGpsStop, this);
    // Forward serial-RTK corrections through NTRIPManager's shared RTCMMavlink so
    // serial and NTRIP sources share one GPS_RTCM_DATA sequence-id domain.
    RTCMMavlink* const rtcmMavlink = NTRIPManager::instance()->rtcmMavlink();
#ifdef Q_OS_HARMONY
    GPSProvider* const provider = _gpsProvider;
    const auto token = _requestGpsStop;
    const auto active = [this, provider, token]() {
        return _gpsProvider == provider && _requestGpsStop == token && !token->load();
    };
    if (rtcmMavlink) {
        const QPointer<RTCMMavlink> pipeline = rtcmMavlink;
        (void) connect(provider, &GPSProvider::RTCMDataUpdate, this, [pipeline, active](const QByteArray& message) {
            if (active() && pipeline) {
                pipeline->RTCMDataUpdate(message);
            }
        });
#else
    if (rtcmMavlink) {
        (void) connect(_gpsProvider, &GPSProvider::RTCMDataUpdate, rtcmMavlink, &RTCMMavlink::RTCMDataUpdate);
#endif
    } else {
        qCWarning(GPSRtkLog) << "Shared RTCMMavlink unavailable; serial RTK corrections will not be forwarded";
    }
#ifdef Q_OS_HARMONY
    (void) connect(provider, &GPSProvider::satelliteInfoUpdate, this, [this, active](const satellite_info_s& message) {
        if (active()) {
            _satelliteInfoUpdate(message);
        }
    });
    (void) connect(provider, &GPSProvider::sensorGpsUpdate, this, [this, active](const sensor_gps_s& message) {
        if (active()) {
            _sensorGpsUpdate(message);
        }
    });
    (void) connect(provider, &GPSProvider::surveyInStatus, this, [this, active](const GPSSurveyInStatus& status) {
        if (active()) {
            _onGPSSurveyInStatus(status);
        }
    });
    (void) connect(provider, &GPSProvider::connectionError, this, [this, active](GPSConnectionError error) {
        if (active()) {
            _onGPSConnectionError(error);
        }
    });
    (void) connect(provider, &QThread::finished, this, [this, active]() {
        if (active()) {
            _onGPSDisconnect();
        }
    });
    (void) connect(provider, &QThread::finished, provider, &QObject::deleteLater);
    (void) connect(provider, &QObject::destroyed, this, [this, provider, token]() {
        if (_gpsProvider == provider && _requestGpsStop == token) {
            _gpsProvider = nullptr;
            _onGPSDisconnect();
        }
    });
#else
    (void) connect(_gpsProvider, &GPSProvider::satelliteInfoUpdate, this, &GPSRtk::_satelliteInfoUpdate);
    (void) connect(_gpsProvider, &GPSProvider::sensorGpsUpdate, this, &GPSRtk::_sensorGpsUpdate);
    (void) connect(_gpsProvider, &GPSProvider::surveyInStatus, this, &GPSRtk::_onGPSSurveyInStatus);
    (void) connect(_gpsProvider, &GPSProvider::connectionError, this, &GPSRtk::_onGPSConnectionError);
    (void) connect(_gpsProvider, &GPSProvider::finished, this, &GPSRtk::_onGPSDisconnect);
#endif

    _onGPSConnect();

    // Start the thread only after every signal is wired, so no early emission is lost.
    (void) QMetaObject::invokeMethod(_gpsProvider, "start", Qt::AutoConnection);
}

void GPSRtk::disconnectGPS()
{
#ifdef Q_OS_HARMONY
    if (GPSProvider* const provider = std::exchange(_gpsProvider, nullptr)) {
        _requestGpsStop->store(true);
        (void) disconnect(provider, nullptr, this, nullptr);
        provider->setParent(nullptr);
        if (provider->wait(kGPSThreadDisconnectTimeout)) {
            provider->deleteLater();
        } else {
            qCWarning(GPSRtkLog) << "GPS thread exit is pending; cleanup will run when it finishes";
        }
    }
    _onGPSDisconnect();
#else
    if (_gpsProvider) {
        _requestGpsStop = true;
        if (_gpsProvider->wait(kGPSThreadDisconnectTimeout)) {
            _gpsProvider->deleteLater();
        } else {
            qCWarning(GPSRtkLog) << "GPS thread did not exit in time; deferring cleanup to finished()";
            (void) _gpsProvider->disconnect(this);  // stale signals must not flip facts after reconnect
            (void) connect(_gpsProvider, &QThread::finished, _gpsProvider, &QObject::deleteLater);
        }
        _gpsProvider = nullptr;
    }
#endif
}

bool GPSRtk::connected() const
{
    return (_gpsProvider ? _gpsProvider->isRunning() : false);
}

FactGroup* GPSRtk::gpsRtkFactGroup()
{
    return _gpsRtkFactGroup;
}

GPSRtk::SatelliteCounts GPSRtk::countSatellites(const satellite_info_s& msg)
{
    SatelliteCounts counts;
    counts.inView = qMin(msg.count, satellite_info_s::SAT_INFO_MAX_SATELLITES);
    for (uint8_t i = 0; i < counts.inView; ++i) {
        if (msg.used[i]) {
            ++counts.used;
        }
    }
    return counts;
}

void GPSRtk::_satelliteInfoUpdate(const satellite_info_s& msg)
{
    const SatelliteCounts counts = countSatellites(msg);
    qCDebug(GPSRtkLog) << Q_FUNC_INFO << QStringLiteral("%1 in view, %2 used").arg(counts.inView).arg(counts.used);
    _gpsRtkFactGroup->numSatellites()->setRawValue(counts.inView);
    _gpsRtkFactGroup->numSatellitesUsed()->setRawValue(counts.used);
}

void GPSRtk::_sensorGpsUpdate(const sensor_gps_s& msg)
{
    qCDebug(GPSRtkLog) << Q_FUNC_INFO
                       << QStringLiteral("alt=%1, long=%2, lat=%3")
                              .arg(msg.altitude_msl_m)
                              .arg(msg.longitude_deg)
                              .arg(msg.latitude_deg);
}
