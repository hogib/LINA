// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned track headers (plain QML, one delegate per track) next to the custom
// ArrangementItem. The headers do not scroll themselves: they follow the item's
// scrollY / trackHeight properties.
import QtQuick
import Lina

Item {
    id: pane

    required property AppController controller
    property bool showHeaders: true
    property bool cullDelegates: false
    readonly property int headerWidth: 190
    property alias view: view

    onVisibleChanged: if (visible) view.forceActiveFocus()

    Rectangle {
        width: pane.headerWidth
        height: view.rulerHeight
        color: "#2c2c2c"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: 10
            color: "#bebebe"
            font.pixelSize: 12
            text: pane.controller.trackCount + " tracks / " + pane.controller.clipCount + " clips"
        }
    }

    Item {
        id: headers
        y: view.rulerHeight
        width: pane.headerWidth
        height: pane.height - y
        clip: true

        Rectangle {
            anchors.fill: parent
            color: "#242424"
        }

        Repeater {
            model: pane.showHeaders ? pane.controller.trackCount : 0
            delegate: Item {
                id: header
                required property int index
                // `revision` is read only to re-evaluate after a command was applied: the
                // plain-struct document has no per-field change signals.
                readonly property bool muted: { pane.controller.revision; return pane.controller.trackMuted(index) }
                readonly property bool soloed: { pane.controller.revision; return pane.controller.trackSoloed(index) }

                y: index * view.trackHeight - view.scrollY
                width: pane.headerWidth
                height: view.trackHeight
                // Not hidden by default, only clipped by the parent. Flipping `visible`
                // while scrolling was measured to make scroll frames several times more
                // expensive in the renderer (bench config "hidden with visible").
                visible: !pane.cullDelegates || (y + height > 0 && y < headers.height)

                Rectangle { width: parent.width; height: 1; color: "#161616" }
                Rectangle { y: 1; width: 6; height: parent.height - 1; color: pane.controller.trackColor(header.index) }
                Text {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: view.trackHeight >= 11
                    color: "#d7d7d7"
                    font.pixelSize: Math.max(8, Math.min(13, view.trackHeight - 4))
                    text: pane.controller.trackName(header.index)
                }
                Repeater {
                    model: view.trackHeight >= 20 ? 2 : 0
                    delegate: Rectangle {
                        id: toggle
                        required property int index
                        readonly property bool on: index === 0 ? header.muted : header.soloed
                        objectName: (index === 0 ? "mute_" : "solo_") + header.index
                        x: pane.headerWidth - (index === 0 ? 63 : 37)
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: Math.min(20, view.trackHeight - 6)
                        radius: 3
                        color: on ? (index === 0 ? "#c9a227" : "#3b8fd9") : "#3a3a3a"
                        Text {
                            anchors.centerIn: parent
                            color: "#e6e6e6"
                            font.pixelSize: 11
                            text: toggle.index === 0 ? "M" : "S"
                        }
                        TapHandler {
                            onTapped: toggle.index === 0 ? pane.controller.toggleMute(header.index)
                                                         : pane.controller.toggleSolo(header.index)
                        }
                    }
                }
            }
        }
    }

    ArrangementItem {
        id: view
        objectName: "arrangement"
        x: pane.headerWidth
        width: pane.width - x
        height: pane.height
        controller: pane.controller
        focus: true
    }
}
