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
import QGroundControl

FlightDisplayViewGStreamer {
    id: videoBackground

    readonly property QtObject videoSink: videoLoader.item && QGroundControl.videoManager.qtVideoOutputEnabled ? videoLoader.item.videoSink : null

    Loader {
        id: videoLoader
        anchors.fill: parent
        active: QGroundControl.videoManager.qtVideoOutputEnabled
        sourceComponent: qtVideoComponent
    }

    Component {
        id: qtVideoComponent
        FlightDisplayViewQtMultimedia {}
    }

}
