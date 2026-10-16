/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

ColumnLayout {
    id: bluetoothSettings
    spacing: _rowSpacing
    property bool _customBleProfile: false
    property string _discoveryError: ""
    readonly property int _bleProfile: bleProfileIndex()
    readonly property bool settingsValid: !subEditConfig.lowEnergy ||
        (serviceField.acceptableInput && writeField.acceptableInput && notifyField.acceptableInput)

    Connections {
        target: subEditConfig
        function onErrorOccurred(errorString) { bluetoothSettings._discoveryError = errorString }
    }

    function bleProfileIndex() {
        var service = subEditConfig.bleServiceUuid.toLowerCase()
        var write = subEditConfig.bleWriteUuid.toLowerCase()
        var notify = subEditConfig.bleNotifyUuid.toLowerCase()
        if (service === "6e400001-b5a3-f393-e0a9-e50e24dcca9e" &&
                write === "6e400002-b5a3-f393-e0a9-e50e24dcca9e" &&
                notify === "6e400003-b5a3-f393-e0a9-e50e24dcca9e") return 0
        if (service === "0000ffe0-0000-1000-8000-00805f9b34fb" &&
                write === "0000ffe1-0000-1000-8000-00805f9b34fb" && notify === write) return 1
        return 2
    }

    function setBleProfile(index) {
        _customBleProfile = index === 2
        if (index === 0) {
            subEditConfig.bleServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
            subEditConfig.bleWriteUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
            subEditConfig.bleNotifyUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
        } else if (index === 1) {
            subEditConfig.bleServiceUuid = "0000ffe0-0000-1000-8000-00805f9b34fb"
            subEditConfig.bleWriteUuid = "0000ffe1-0000-1000-8000-00805f9b34fb"
            subEditConfig.bleNotifyUuid = "0000ffe1-0000-1000-8000-00805f9b34fb"
        }
        if (index !== 2) {
            serviceField.text = subEditConfig.bleServiceUuid
            writeField.text = subEditConfig.bleWriteUuid
            notifyField.text = subEditConfig.bleNotifyUuid
        }
    }

    function saveSettings() {
        if (!settingsValid) return false
        if (subEditConfig.lowEnergy) {
            var service = serviceField.text.trim()
            var write = writeField.text.trim()
            var notify = notifyField.text.trim()
            subEditConfig.bleServiceUuid = service
            subEditConfig.bleWriteUuid = write
            subEditConfig.bleNotifyUuid = notify
        }
        return true
    }

    RegularExpressionValidator {
        id: uuidValidator
        regularExpression: /^\s*(?:[0-9a-fA-F]{4}|[0-9a-fA-F]{8}|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\s*$/
    }

    GridLayout {
        columns:    2
        columnSpacing:  _colSpacing
        rowSpacing:     _rowSpacing

        QGCLabel {
            text: qsTr("Bluetooth mode")
            visible: subEditConfig.supportsLowEnergy
        }
        QGCComboBox {
            objectName: "bluetoothTransport"
            Layout.preferredWidth: _secondColumnWidth
            visible: subEditConfig.supportsLowEnergy
            model: [qsTr("Classic SPP"), qsTr("Bluetooth LE (GATT)")]
            currentIndex: subEditConfig.lowEnergy ? 1 : 0
            onActivated: (index) => {
                bluetoothSettings._customBleProfile = false
                subEditConfig.lowEnergy = index === 1
            }
        }

        QGCLabel {
            text: qsTr("BLE profile")
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
        }
        QGCComboBox {
            objectName: "bleProfile"
            Layout.preferredWidth: _secondColumnWidth
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
            model: [qsTr("Nordic UART"), qsTr("HM-10"), qsTr("Custom")]
            currentIndex: bluetoothSettings._customBleProfile ? 2 : bluetoothSettings._bleProfile
            onActivated: (index) => bluetoothSettings.setBleProfile(index)
        }

        QGCLabel {
            text: qsTr("Service UUID")
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
        }
        QGCTextField {
            id: serviceField
            objectName: "bleServiceUuid"
            Layout.preferredWidth: _secondColumnWidth
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
            text: subEditConfig.bleServiceUuid
            readOnly: !bluetoothSettings._customBleProfile && bluetoothSettings._bleProfile !== 2
            validator: uuidValidator
            validationError: subEditConfig.lowEnergy && !acceptableInput
            onEditingFinished: if (acceptableInput) subEditConfig.bleServiceUuid = text.trim()
        }

        QGCLabel {
            text: qsTr("Write UUID")
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
        }
        QGCTextField {
            id: writeField
            objectName: "bleWriteUuid"
            Layout.preferredWidth: _secondColumnWidth
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
            text: subEditConfig.bleWriteUuid
            readOnly: !bluetoothSettings._customBleProfile && bluetoothSettings._bleProfile !== 2
            validator: uuidValidator
            validationError: subEditConfig.lowEnergy && !acceptableInput
            onEditingFinished: if (acceptableInput) subEditConfig.bleWriteUuid = text.trim()
        }

        QGCLabel {
            text: qsTr("Notify UUID")
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
        }
        QGCTextField {
            id: notifyField
            objectName: "bleNotifyUuid"
            Layout.preferredWidth: _secondColumnWidth
            visible: subEditConfig.supportsLowEnergy && subEditConfig.lowEnergy
            text: subEditConfig.bleNotifyUuid
            readOnly: !bluetoothSettings._customBleProfile && bluetoothSettings._bleProfile !== 2
            validator: uuidValidator
            validationError: subEditConfig.lowEnergy && !acceptableInput
            onEditingFinished: if (acceptableInput) subEditConfig.bleNotifyUuid = text.trim()
        }

        QGCLabel { text: qsTr("Device") }
        QGCLabel {
            Layout.preferredWidth:  _secondColumnWidth
            text:                   subEditConfig.deviceName
        }

        QGCLabel { text: qsTr("Address") }
        QGCLabel {
            Layout.preferredWidth:  _secondColumnWidth
            text:                   subEditConfig.address
        }
    }

    QGCLabel { text: qsTr("Bluetooth Devices") }

    Repeater {
        model: subEditConfig.nameList

        delegate: QGCButton {
            text:                   modelData
            Layout.preferredWidth: _secondColumnWidth
            autoExclusive:          true

            onClicked: {
                checked = true
                if (modelData !== "")
                    subEditConfig.setDevice(modelData)
            }
        }
    }

    QGCLabel {
        Layout.fillWidth: true
        Layout.preferredWidth: _secondColumnWidth
        text: bluetoothSettings._discoveryError
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        visible: text.length > 0
    }

    RowLayout {
        Layout.alignment:   Qt.AlignCenter
        spacing:            _colSpacing

        QGCButton {
            text:       qsTr("Scan")
            enabled:    !subEditConfig.scanning
            onClicked: {
                bluetoothSettings._discoveryError = ""
                subEditConfig.startScan()
            }
        }

        QGCButton {
            text:       qsTr("Stop")
            enabled:    subEditConfig.scanning
            onClicked:  subEditConfig.stopScan()
        }
    }
}
