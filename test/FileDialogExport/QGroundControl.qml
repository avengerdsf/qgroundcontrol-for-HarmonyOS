pragma Singleton
import QtQml

QtObject {
    property QtObject corePlugin: QtObject {
        property QtObject options: QtObject {
            property bool useMobileFileDialog: false
        }
    }
    property QtObject settingsManager: QtObject {
        property QtObject appSettings: QtObject {
            property string logSavePath
            property QtObject gstDebugLevel: QtObject {
                property bool visible: false
            }
        }
    }
    function loggingCategories() { return [] }
}
