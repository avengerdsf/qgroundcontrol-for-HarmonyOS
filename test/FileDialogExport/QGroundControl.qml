pragma Singleton
import QtQml

QtObject {
    property QtObject corePlugin: QtObject {
        property QtObject options: QtObject {
            property bool useMobileFileDialog: false
        }
    }
    property QtObject settingsManager: QtObject {
        property QtObject logManagerSettings: QtObject { property QtObject saveFormat: QtObject { property int rawValue: 0 } }
        property QtObject appSettings: QtObject {
            property string logSavePath
            property QtObject gstDebugLevel: QtObject {
                property bool visible: false
            }
        }
    }
    function loggingCategories() { return [] }
}
