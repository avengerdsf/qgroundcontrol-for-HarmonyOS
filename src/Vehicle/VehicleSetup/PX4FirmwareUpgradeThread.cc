#include "PX4FirmwareUpgradeThread.h"

#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QVariantMap>

#include "Bootloader.h"
#include "FirmwareImage.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(PX4FirmwareUpgradeThreadLog, "Vehicle.VehicleSetup.PX4FirmwareUpgradeThread")
QGC_LOGGING_CATEGORY(PX4FirmwareUpgradeThreadVerboseLog, "Vehicle.VehicleSetup.PX4FirmwareUpgradeThread.Verbose")
#include <QtCore/QTimer>

PX4FirmwareUpgradeThreadWorker::PX4FirmwareUpgradeThreadWorker(PX4FirmwareUpgradeThreadController* controller)
    : _controller(controller)
{
    connect(_controller, &PX4FirmwareUpgradeThreadController::_initThreadWorker,            this, &PX4FirmwareUpgradeThreadWorker::_init);
    connect(_controller, &PX4FirmwareUpgradeThreadController::_startFindBoardLoopOnThread,  this, &PX4FirmwareUpgradeThreadWorker::_startFindBoardLoop);
    connect(_controller, &PX4FirmwareUpgradeThreadController::_flashOnThread,               this, &PX4FirmwareUpgradeThreadWorker::_flash);
    connect(_controller, &PX4FirmwareUpgradeThreadController::_rebootOnThread,              this, &PX4FirmwareUpgradeThreadWorker::_reboot);
    connect(_controller, &PX4FirmwareUpgradeThreadController::_cancel,                      this, &PX4FirmwareUpgradeThreadWorker::_cancel);
    connect(_controller, &PX4FirmwareUpgradeThreadController::_setTargetPortOnThread,       this, &PX4FirmwareUpgradeThreadWorker::_setTargetPort);
}

PX4FirmwareUpgradeThreadWorker::~PX4FirmwareUpgradeThreadWorker()
{

}

void PX4FirmwareUpgradeThreadWorker::_init(void)
{
    // We create the timers here so that they are on the right thread

    _findBoardTimer = new QTimer(this);
    _findBoardTimer->setSingleShot(true);
    _findBoardTimer->setInterval(500);
    connect(_findBoardTimer, &QTimer::timeout, this, &PX4FirmwareUpgradeThreadWorker::_findBoardOnce);
}

void PX4FirmwareUpgradeThreadWorker::_cancel(void)
{
    qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "_cancel";
    _targetSystemLocation.clear();
    _targetSerialNumber.clear();
    _targetSeenAtLeastOnce = false;
    _ignoredPorts.clear();
    _foundBoard = false;
    if (_bootloader) {
#ifndef Q_OS_HARMONY
        _bootloader->reboot();
#endif
        _bootloader->close();
        _bootloader->deleteLater();
        _bootloader = nullptr;
    }
    if (!_shuttingDown && _findBoardTimer && !_findBoardTimer->isActive()) {
        _findBoardTimer->start();
    }
}

void PX4FirmwareUpgradeThreadWorker::_shutdown(void)
{
    _shuttingDown = true;
    _cancel();
    if (_findBoardTimer) {
        _findBoardTimer->stop();
    }
}

void PX4FirmwareUpgradeThreadWorker::_startFindBoardLoop(void)
{
#ifdef Q_OS_HARMONY
    _reselectAfterIdentityLoss = false;
#endif
    _foundBoard = false;
    _findBoardFirstAttempt = true;
    _targetSystemLocation.clear();
    _targetSerialNumber.clear();
    _targetSeenAtLeastOnce = false;
    _ignoredPorts.clear();
    _findBoardOnce();
}

void PX4FirmwareUpgradeThreadWorker::_resumeDiscovery(void)
{
    _targetSystemLocation.clear();
    _targetSerialNumber.clear();
    _targetSeenAtLeastOnce = false;
    _ignoredPorts.clear();
    _foundBoard = false;
    if (!_shuttingDown && _findBoardTimer && !_findBoardTimer->isActive()) {
        _findBoardTimer->start();
    }
}

void PX4FirmwareUpgradeThreadWorker::_setTargetPort(const QString& systemLocation)
{
    qCDebug(PX4FirmwareUpgradeThreadLog) << "_setTargetPort" << systemLocation;
    _targetSystemLocation = systemLocation;
    _targetSerialNumber.clear();
    _targetSeenAtLeastOnce = false;
    _foundBoard = false;
    _ignoredPorts.clear();

    bool currentlyVisible = false;
    bool currentlyInBootloader = false;
    const auto selectionPorts = QGCSerialPortInfo::availablePorts();
    for (const QGCSerialPortInfo& info : selectionPorts) {
        if (info.systemLocation() == systemLocation) {
            currentlyVisible = true;
            // Capture serial number so we can find the board across a path change
            // when it re-enumerates as a bootloader on a different /dev/ttyACM*.
            _targetSerialNumber = info.serialNumber();
            // If the descriptor already looks like a bootloader, skip the unplug-replug
            // dance and handshake immediately on the next poll.
            currentlyInBootloader = info.description().contains(QStringLiteral("BL"))
                                  || info.description().contains(QStringLiteral("Bootloader"), Qt::CaseInsensitive);
        } else {
            // Remember which ports were already there so we don't accidentally attach
            // to one of them via the canFlash() fallback after the target disappears.
            _ignoredPorts.insert(info.systemLocation());
        }
    }
#ifdef Q_OS_HARMONY
    QSet<QString> matchingDevices;
    for (const QGCSerialPortInfo& info : selectionPorts) {
        if (info.serialNumber() == _targetSerialNumber) {
            const QString deviceName = info.deviceName();
            matchingDevices.insert(deviceName.isEmpty() ? QStringLiteral("port:") + info.systemLocation()
                                                        : QStringLiteral("usb:") + deviceName);
        }
    }
    // Later enumeration cannot establish which physical device owned an ambiguous serial at selection.
    if (_targetSerialNumber == QStringLiteral("0") || matchingDevices.size() != 1) {
        _targetSerialNumber.clear();
    }
    if (!currentlyVisible) {
        _resumeDiscovery();
        emit error(tr("The selected port is no longer present. Please select the target port again."));
        return;
    }
#endif
#ifdef Q_OS_HARMONY
    _findBoardFirstAttempt = currentlyVisible && !currentlyInBootloader && !_reselectAfterIdentityLoss;
    _reselectAfterIdentityLoss = false;
#else
    _findBoardFirstAttempt = currentlyVisible && !currentlyInBootloader;
#endif
}

QVariantList PX4FirmwareUpgradeThreadWorker::_buildPortDescriptors() const
{
    QVariantList list;

    for (const QGCSerialPortInfo& info : QGCSerialPortInfo::availablePorts()) {
        QGCSerialPortInfo::BoardType_t boardType;
        QString boardName;
        const bool recognized = info.getBoardInfo(boardType, boardName);

        QVariantMap entry;
        entry["systemLocation"] = info.systemLocation();
        entry["portName"]       = info.portName();
        entry["description"]    = info.description();
        entry["manufacturer"]   = info.manufacturer();
        entry["boardType"]      = static_cast<int>(recognized ? boardType : QGCSerialPortInfo::BoardTypeUnknown);
        entry["boardName"]      = recognized ? boardName : QString();
        entry["recognized"]     = recognized;

        QString display = info.portName();
        if (!info.description().isEmpty()) {
            display += QStringLiteral(" — ") + info.description();
        }
        if (recognized && !boardName.isEmpty()) {
            display += QStringLiteral(" [") + boardName + QStringLiteral("]");
        }
        entry["displayName"] = display;

        list.append(entry);
    }

    return list;
}

void PX4FirmwareUpgradeThreadWorker::_findBoardOnce(void)
{
    qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "_findBoardOnce";
    if (_shuttingDown) {
        return;
    }

    emit portsAvailable(_buildPortDescriptors());

    if (_targetSystemLocation.isEmpty()) {
        _findBoardTimer->start();
        return;
    }

#ifdef Q_OS_HARMONY
    if (_controller->_requestStop) {
        _resumeDiscovery();
        return;
    }
#endif
    QGCSerialPortInfo               targetPort;
    QGCSerialPortInfo::BoardType_t  boardType   = QGCSerialPortInfo::BoardTypeUnknown;
    QString                         boardName;
    bool                            foundTarget = false;

    const auto ports = QGCSerialPortInfo::availablePorts();
#ifdef Q_OS_HARMONY
    QString verifiedTargetLocation = _targetSystemLocation;
    if (!_targetSerialNumber.isEmpty()) {
        QStringList matchingLocations;
        for (const QGCSerialPortInfo& info : ports) {
            if (info.serialNumber() == _targetSerialNumber) {
                matchingLocations.append(info.systemLocation());
            }
        }
        const bool selectedPortStillPresent =
            (!_targetSeenAtLeastOnce || _foundBoard) && matchingLocations.contains(_targetSystemLocation);
        if (!selectedPortStillPresent) {
            if (matchingLocations.size() > 1) {
                _reselectAfterIdentityLoss = true;
                _resumeDiscovery();
                emit error(tr("Multiple ports share the selected USB identity. Please select the target port again."));
                return;
            }
            verifiedTargetLocation = matchingLocations.isEmpty() ? QString{} : matchingLocations.first();
        }
    } else if (_targetSeenAtLeastOnce && !_foundBoard) {
        verifiedTargetLocation.clear();
    }
#endif
    for (const QGCSerialPortInfo& info : ports) {
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "Serial Port --------------";
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "\tport name:" << info.portName();
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "\tdescription:" << info.description();
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "\tsystem location:" << info.systemLocation();
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "\tvendor ID:" << info.vendorIdentifier();
        qCDebug(PX4FirmwareUpgradeThreadVerboseLog) << "\tproduct ID:" << info.productIdentifier();

#ifdef Q_OS_HARMONY
        const bool match = !verifiedTargetLocation.isEmpty() && info.systemLocation() == verifiedTargetLocation;
#else
        bool match = (info.systemLocation() == _targetSystemLocation);
        if (!match && !_targetSerialNumber.isEmpty() && !info.serialNumber().isEmpty()) {
            match = (info.serialNumber() == _targetSerialNumber);
        }
        // After we've seen the target at least once and it's now gone, accept any
        // recognized flashable port that wasn't already there at click time —
        // covers a board re-enumerating as a bootloader at a different /dev/ttyACM*
        // path with a different VID/PID, while not attaching to other unrelated
        // flashable devices that were already plugged in.
        if (!match && _targetSeenAtLeastOnce && !_foundBoard
            && info.canFlash()
            && !_ignoredPorts.contains(info.systemLocation())) {
            match = true;
        }
#endif

        if (match) {
            targetPort = info;
            info.getBoardInfo(boardType, boardName);
            foundTarget = true;
            break;
        }
    }

    if (foundTarget) {
        if (!_foundBoard) {
            _foundBoard = true;
            _targetSeenAtLeastOnce = true;
            _foundBoardPortInfo = targetPort;
#ifndef Q_OS_HARMONY
            if (_targetSerialNumber.isEmpty() && !targetPort.serialNumber().isEmpty()) {
                _targetSerialNumber = targetPort.serialNumber();
            }
#endif
            emit foundBoard(_findBoardFirstAttempt, targetPort, boardType, boardName);
            if (!_findBoardFirstAttempt) {
#ifdef Q_OS_HARMONY
                _bootloader =
                    new Bootloader(boardType == QGCSerialPortInfo::BoardTypeSiKRadio, this, &_controller->_requestStop);
#else
                _bootloader = new Bootloader(boardType == QGCSerialPortInfo::BoardTypeSiKRadio, this);
#endif
                connect(_bootloader, &Bootloader::updateProgress, this, &PX4FirmwareUpgradeThreadWorker::_updateProgress);

                if (_bootloader->open(targetPort.portName())) {
                    uint32_t    bootloaderVersion;
                    uint32_t    boardId;
                    uint32_t    flashSize;
                    if (_bootloader->getBoardInfo(bootloaderVersion, boardId, flashSize)) {
                        emit foundBoardInfo(bootloaderVersion, boardId, flashSize);
                    } else {
                        emit error(_bootloader->errorString());
                    }
                } else {
                    emit error(_bootloader->errorString());
                }
                return;
            }
        }
    } else {
        if (_foundBoard) {
            _foundBoard = false;
            qCDebug(PX4FirmwareUpgradeThreadLog) << "Target port gone";
            emit boardGone();
#ifdef Q_OS_HARMONY
            _reselectAfterIdentityLoss = true;
            if (!_targetSerialNumber.isEmpty()) {
                emit status(tr(
                    "Waiting for the selected USB device. Select the target port again if its USB identity changed."));
            }
#endif
        }
#ifdef Q_OS_HARMONY
        if (_targetSerialNumber.isEmpty()) {
            _reselectAfterIdentityLoss = true;
            _resumeDiscovery();
            emit error(tr("The disconnected device has no stable USB identity. Please select the target port again."));
            return;
        }
#endif
    }

    _findBoardFirstAttempt = false;
    _findBoardTimer->start();
}

void PX4FirmwareUpgradeThreadWorker::_reboot(void)
{
#ifdef Q_OS_HARMONY
    if (!_bootloader || _controller->_requestStop) {
        return;
    }
#endif
    _bootloader->reboot();
}

void PX4FirmwareUpgradeThreadWorker::_flash(void)
{
    qCDebug(PX4FirmwareUpgradeThreadLog) << "PX4FirmwareUpgradeThreadWorker::_flash";

    if (!_bootloader) {
        // Cancelled between download completion and the queued flash slot firing.
        qCDebug(PX4FirmwareUpgradeThreadLog) << "_flash: bootloader gone, aborting";
        _resumeDiscovery();
        return;
    }

#ifdef Q_OS_HARMONY
    if (_controller->_requestStop) {
        goto Error;
    }
#endif
    if (!_bootloader->initFlashSequence()) {
#ifdef Q_OS_HARMONY
        goto Error;
#else
        emit error(_bootloader->errorString());
        _resumeDiscovery();
        return;
#endif
    }

    if (_erase()) {
        emit status(tr("Programming new version..."));

        if (_bootloader->program(_controller->image())) {
            qCDebug(PX4FirmwareUpgradeThreadLog) << "Program complete";
        } else {
            qCDebug(PX4FirmwareUpgradeThreadLog) << "Program failed:" << _bootloader->errorString();
            goto Error;
        }

        emit status(tr("Verifying program..."));

        if (_bootloader->verify(_controller->image())) {
            qCDebug(PX4FirmwareUpgradeThreadLog) << "Verify complete";
        } else {
            qCDebug(PX4FirmwareUpgradeThreadLog) << "Verify failed:" << _bootloader->errorString();
            goto Error;
        }
    } else {
        goto Error;
    }
#ifdef Q_OS_HARMONY
    if (_controller->_requestStop) {
        goto Error;
    }
#endif

    emit status(tr("Rebooting board"));
    _reboot();

    _bootloader->close();
    _bootloader->deleteLater();
    _bootloader = nullptr;

    emit flashComplete();

    _resumeDiscovery();
    return;

Error:
    emit error(_bootloader->errorString());
#ifndef Q_OS_HARMONY
    _reboot();
#endif
    _bootloader->close();
    _bootloader->deleteLater();
    _bootloader = nullptr;
    _resumeDiscovery();
}

bool PX4FirmwareUpgradeThreadWorker::_erase(void)
{
    qCDebug(PX4FirmwareUpgradeThreadLog) << "PX4FirmwareUpgradeThreadWorker::_erase";

    emit eraseStarted();
    emit status(tr("Erasing previous program..."));

    if (_bootloader->erase()) {
        qCDebug(PX4FirmwareUpgradeThreadLog) << "Erase complete";
        emit status(tr("Erase complete"));
        emit eraseComplete();
        return true;
    } else {
        qCDebug(PX4FirmwareUpgradeThreadLog) << "Erase failed:" << _bootloader->errorString();
        emit error(_bootloader->errorString());
        return false;
    }
}

PX4FirmwareUpgradeThreadController::PX4FirmwareUpgradeThreadController(QObject* parent) :
    QObject(parent)
{
    _worker         = new PX4FirmwareUpgradeThreadWorker(this);
    _workerThread   = new QThread(this);
    _worker->moveToThread(_workerThread);
    connect(_workerThread, &QThread::finished, _worker, &QObject::deleteLater);

    connect(_worker, &PX4FirmwareUpgradeThreadWorker::updateProgress,       this, &PX4FirmwareUpgradeThreadController::_updateProgress);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::foundBoard,           this, &PX4FirmwareUpgradeThreadController::_foundBoard);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::noBoardFound,         this, &PX4FirmwareUpgradeThreadController::_noBoardFound);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::boardGone,            this, &PX4FirmwareUpgradeThreadController::_boardGone);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::foundBoardInfo,       this, &PX4FirmwareUpgradeThreadController::_foundBoardInfo);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::error,                this, &PX4FirmwareUpgradeThreadController::_error);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::status,               this, &PX4FirmwareUpgradeThreadController::_status);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::eraseStarted,         this, &PX4FirmwareUpgradeThreadController::_eraseStarted);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::eraseComplete,        this, &PX4FirmwareUpgradeThreadController::_eraseComplete);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::flashComplete,        this, &PX4FirmwareUpgradeThreadController::_flashComplete);
    connect(_worker, &PX4FirmwareUpgradeThreadWorker::portsAvailable,       this, &PX4FirmwareUpgradeThreadController::_portsAvailable);

    _workerThread->start();

    emit _initThreadWorker();
}

PX4FirmwareUpgradeThreadController::~PX4FirmwareUpgradeThreadController()
{
#ifdef Q_OS_HARMONY
    ++_cancelGeneration;
    _requestStop = true;
#endif
    QMetaObject::invokeMethod(_worker, "_shutdown", Qt::BlockingQueuedConnection);
    _workerThread->quit();
    _workerThread->wait();

    delete _workerThread;
}

void PX4FirmwareUpgradeThreadController::startFindBoardLoop(void)
{
    qCDebug(PX4FirmwareUpgradeThreadLog) << "PX4FirmwareUpgradeThreadController::findBoard";
#ifdef Q_OS_HARMONY
    const quint64 generation = _cancelGeneration.load();
    QMetaObject::invokeMethod(
        _worker,
        [this, generation]() {
            if (_cancelGeneration.load() != generation) {
                return;
            }
            _requestStop = false;
            if (_cancelGeneration.load() != generation) {
                _requestStop = true;
                return;
            }
            emit _startFindBoardLoopOnThread();
        },
        Qt::QueuedConnection);
#else
    emit _startFindBoardLoopOnThread();
#endif
}

void PX4FirmwareUpgradeThreadController::cancel(void)
{
    qCDebug(PX4FirmwareUpgradeThreadLog) << "PX4FirmwareUpgradeThreadController::cancel";
#ifdef Q_OS_HARMONY
    ++_cancelGeneration;
    _requestStop = true;
#endif
    emit _cancel();
}

void PX4FirmwareUpgradeThreadController::setTargetPort(const QString& systemLocation)
{
#ifdef Q_OS_HARMONY
    const quint64 generation = _cancelGeneration.load();
    QMetaObject::invokeMethod(
        _worker,
        [this, generation, systemLocation]() {
            if (_cancelGeneration.load() != generation) {
                return;
            }
            _requestStop = false;
            if (_cancelGeneration.load() != generation) {
                _requestStop = true;
                return;
            }
            emit _setTargetPortOnThread(systemLocation);
        },
        Qt::QueuedConnection);
#else
    emit _setTargetPortOnThread(systemLocation);
#endif
}

void PX4FirmwareUpgradeThreadController::flash(const FirmwareImage* image)
{
    _image = image;
    emit _flashOnThread();
}
