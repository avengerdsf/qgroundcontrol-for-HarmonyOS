pragma Singleton

import QtQml

QtObject {
    property QtObject multiVehicleManager: QtObject { property var activeVehicle: null }
    property QtObject corePlugin: QtObject {
        property QtObject options: QtObject {
            property bool enableSaveMainWindowPosition: true
        }
    }
}
