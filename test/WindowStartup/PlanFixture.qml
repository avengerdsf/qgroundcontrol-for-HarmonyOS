import QtQuick
Item {
    property bool isPlanFixture: true
    objectName: "PlanFixture"
    property QtObject _planMasterController: QtObject {
        property bool dirty: false
        property bool dirtyForSave: dirty
        property bool dirtyForUpload: dirty
    }
}
