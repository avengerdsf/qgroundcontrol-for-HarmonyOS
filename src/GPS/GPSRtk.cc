/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GPSRtk.h"
#include "GPSProvider.h"
#include "GPSRTKFactGroup.h"
#include "QGCLoggingCategory.h"
#include "RTCMMavlink.h"
#include "RTKSettings.h"
#include "SettingsManager.h"
#include <utility>

QGC_LOGGING_CATEGORY(GPSRtkLog, "qgc.gps.gpsrtk")

GPSRtk::GPSRtk(QObject *parent)
    : QObject(parent)
    , _gpsRtkFactGroup(new GPSRTKFactGroup(this))
{
    // qCDebug(GPSRtkLog) << Q_FUNC_INFO << this;
}

GPSRtk::~GPSRtk()
{
    disconnectGPS();

    // qCDebug(GPSRtkLog) << Q_FUNC_INFO << this;
}

void GPSRtk::registerQmlTypes()
{
    (void) qRegisterMetaType<satellite_info_s>("satellite_info_s");
    (void) qRegisterMetaType<sensor_gnss_relative_s>("sensor_gnss_relative_s");
    (void) qRegisterMetaType<sensor_gps_s>("sensor_gps_s");
}

void GPSRtk::_onGPSConnect()
{
    _gpsRtkFactGroup->connected()->setRawValue(true);
}

void GPSRtk::_onGPSDisconnect()
{
    _gpsRtkFactGroup->connected()->setRawValue(false);
}

void GPSRtk::_onGPSSurveyInStatus(float duration, float accuracyMM,  double latitude, double longitude, float altitude, bool valid, bool active)
{
    _gpsRtkFactGroup->currentDuration()->setRawValue(duration);
    _gpsRtkFactGroup->currentAccuracy()->setRawValue(static_cast<double>(accuracyMM) / 1000.0);
    _gpsRtkFactGroup->currentLatitude()->setRawValue(latitude);
    _gpsRtkFactGroup->currentLongitude()->setRawValue(longitude);
    _gpsRtkFactGroup->currentAltitude()->setRawValue(altitude);
    _gpsRtkFactGroup->valid()->setRawValue(valid);
    _gpsRtkFactGroup->active()->setRawValue(active);
}

void GPSRtk::connectGPS(const QString &device, QStringView gps_type)
{
    GPSProvider::GPSType type;
    if (gps_type.contains(QStringLiteral("trimble"), Qt::CaseInsensitive)) {
        type = GPSProvider::GPSType::trimble;
        qCDebug(GPSRtkLog) << "Connecting Trimble device";
    } else if (gps_type.contains(QStringLiteral("septentrio"), Qt::CaseInsensitive)) {
        type = GPSProvider::GPSType::septentrio;
        qCDebug(GPSRtkLog) << "Connecting Septentrio device";
    } else if (gps_type.contains(QStringLiteral("femtomes"), Qt::CaseInsensitive)) {
        type = GPSProvider::GPSType::femto;
        qCDebug(GPSRtkLog) << "Connecting Femtomes device";
    } else {
        type = GPSProvider::GPSType::u_blox;
        qCDebug(GPSRtkLog) << "Connecting U-blox device";
    }

    disconnectGPS();

    RTKSettings* const rtkSettings = SettingsManager::instance()->rtkSettings();
#ifdef Q_OS_HARMONY
    _requestGpsStop = std::make_shared<std::atomic_bool>(false);
#else
    _requestGpsStop = false;
#endif
    const GPSProvider::rtk_data_s rtkData = {
        rtkSettings->surveyInAccuracyLimit()->rawValue().toDouble(),
        rtkSettings->surveyInMinObservationDuration()->rawValue().toInt(),
        rtkSettings->useFixedBasePosition()->rawValue().toBool(),
        rtkSettings->fixedBasePositionLatitude()->rawValue().toDouble(),
        rtkSettings->fixedBasePositionLongitude()->rawValue().toDouble(),
        rtkSettings->fixedBasePositionAltitude()->rawValue().toFloat(),
        rtkSettings->fixedBasePositionAccuracy()->rawValue().toFloat()
    };
    _gpsProvider = new GPSProvider(
        device,
        type,
        rtkData,
        _requestGpsStop,
        this
    );
#ifndef Q_OS_HARMONY
    (void) QMetaObject::invokeMethod(_gpsProvider, "start", Qt::AutoConnection);
#endif

    _rtcmMavlink = new RTCMMavlink(this);
#ifdef Q_OS_HARMONY
    GPSProvider *const provider = _gpsProvider;
    RTCMMavlink *const pipeline = _rtcmMavlink;
    const auto token = _requestGpsStop;
    const auto active = [this, provider, token]() {
        return _gpsProvider == provider && _requestGpsStop == token && !token->load();
    };
    (void) connect(provider, &GPSProvider::RTCMDataUpdate, this, [this, pipeline, active](const QByteArray &message) {
        if (active() && _rtcmMavlink == pipeline) { pipeline->RTCMDataUpdate(message); }
    });
    (void) connect(provider, &GPSProvider::satelliteInfoUpdate, this, [this, active](const satellite_info_s &message) {
        if (active()) { _satelliteInfoUpdate(message); }
    });
    (void) connect(provider, &GPSProvider::sensorGpsUpdate, this, [this, active](const sensor_gps_s &message) {
        if (active()) { _sensorGpsUpdate(message); }
    });
    (void) connect(provider, &GPSProvider::surveyInStatus, this,
        [this, active](float duration, float accuracy, double latitude, double longitude, float altitude, bool valid, bool running) {
            if (active()) { _onGPSSurveyInStatus(duration, accuracy, latitude, longitude, altitude, valid, running); }
        });
    (void) connect(provider, &QThread::finished, this, [this, provider, token]() {
        if (_gpsProvider == provider && _requestGpsStop == token) { _onGPSDisconnect(); }
    });
    (void) connect(provider, &QObject::destroyed, this, [this, provider, token]() {
        if (_gpsProvider == provider && _requestGpsStop == token) {
            _gpsProvider = nullptr;
            _onGPSDisconnect();
        }
    });
    (void) connect(provider, &QThread::finished, provider, &QObject::deleteLater);
    (void) QMetaObject::invokeMethod(provider, "start", Qt::AutoConnection);
#else
    (void) connect(_gpsProvider, &GPSProvider::RTCMDataUpdate, _rtcmMavlink, &RTCMMavlink::RTCMDataUpdate);

    (void) connect(_gpsProvider, &GPSProvider::satelliteInfoUpdate, this, &GPSRtk::_satelliteInfoUpdate);
    (void) connect(_gpsProvider, &GPSProvider::sensorGpsUpdate, this, &GPSRtk::_sensorGpsUpdate);
    (void) connect(_gpsProvider, &GPSProvider::surveyInStatus, this, &GPSRtk::_onGPSSurveyInStatus);
    (void) connect(_gpsProvider, &GPSProvider::finished, this, &GPSRtk::_onGPSDisconnect);
#endif

    (void) QMetaObject::invokeMethod(this, "_onGPSConnect", Qt::AutoConnection);
}

void GPSRtk::disconnectGPS()
{
#ifdef Q_OS_HARMONY
    if (GPSProvider *const provider = std::exchange(_gpsProvider, nullptr)) {
        _requestGpsStop->store(true);
        (void) disconnect(provider, nullptr, this, nullptr);
        if (_rtcmMavlink) { (void) disconnect(provider, nullptr, _rtcmMavlink, nullptr); }
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
        if (!_gpsProvider->wait(kGPSThreadDisconnectTimeout)) {
            qCWarning(GPSRtkLog) << "Failed to wait for GPS thread exit. Consider increasing the timeout";
        }

        _gpsProvider->deleteLater();
        _gpsProvider = nullptr;
    }
#endif

    if (_rtcmMavlink) {
        _rtcmMavlink->deleteLater();
        _rtcmMavlink = nullptr;
    }
}

bool GPSRtk::connected() const
{
    return (_gpsProvider ? _gpsProvider->isRunning() : false);
}

FactGroup *GPSRtk::gpsRtkFactGroup()
{
    return _gpsRtkFactGroup;
}

void GPSRtk::_satelliteInfoUpdate(const satellite_info_s &msg)
{
    qCDebug(GPSRtkLog) << Q_FUNC_INFO << QStringLiteral("%1 satellites").arg(msg.count);
    _gpsRtkFactGroup->numSatellites()->setRawValue(msg.count);
}

void GPSRtk::_sensorGnssRelativeUpdate(const sensor_gnss_relative_s &msg)
{
    qCDebug(GPSRtkLog) << Q_FUNC_INFO;
}

void GPSRtk::_sensorGpsUpdate(const sensor_gps_s &msg)
{
    qCDebug(GPSRtkLog) << Q_FUNC_INFO << QStringLiteral("alt=%1, long=%2, lat=%3").arg(msg.altitude_msl_m).arg(msg.longitude_deg).arg(msg.latitude_deg);
}
