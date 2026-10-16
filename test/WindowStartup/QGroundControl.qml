pragma Singleton

import QtQml

QtObject {
    property QtObject corePlugin: QtObject {
        property QtObject options: QtObject {
            property bool enableSaveMainWindowPosition: true
        }
    }
}
