// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned keyboard column (plain QML, one delegate per key) next to the custom
// PianoRollItem, following its scrollY / keyHeight properties.
import QtQuick
import Lina

Item {
    id: pane

    required property AppController controller
    property bool showKeys: true
    property bool cullDelegates: false
    readonly property int keyWidth: 72
    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    property alias view: view

    onVisibleChanged: if (visible) view.forceActiveFocus()

    Rectangle {
        width: pane.keyWidth
        height: pane.height
        color: "#2c2c2c"
        Text {
            x: 8
            y: pane.height - view.laneHeight + 8
            color: "#bebebe"
            font.pixelSize: 12
            text: "Velocity"
        }
    }

    Item {
        id: keys
        y: view.rulerHeight
        width: pane.keyWidth
        height: pane.height - view.rulerHeight - view.laneHeight
        clip: true

        Rectangle {
            anchors.fill: parent
            color: "#e1e1e1"
        }

        Repeater {
            model: pane.showKeys ? 128 : 0
            delegate: Item {
                id: key
                required property int index
                readonly property int pitch: 127 - index
                readonly property int pc: pitch % 12
                readonly property bool black: pc === 1 || pc === 3 || pc === 6 || pc === 8 || pc === 10

                y: index * view.keyHeight - view.scrollY
                width: pane.keyWidth
                height: view.keyHeight
                // Not hidden by default, only clipped by the parent. Flipping `visible`
                // while scrolling was measured to make scroll frames several times more
                // expensive in the renderer (bench config "hidden with visible").
                visible: !pane.cullDelegates || (y + height > 0 && y < keys.height)

                Rectangle {
                    visible: key.black
                    width: parent.width * 0.62
                    height: parent.height
                    color: "#191919"
                }
                Rectangle {
                    visible: key.pc === 0 || key.pc === 5
                    y: parent.height
                    width: parent.width
                    height: 1
                    color: "#787878"
                }
                Text {
                    visible: key.pc === 0 || view.keyHeight >= 14
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: key.pc === 0 ? "#141414" : "#6e6e6e"
                    font.pixelSize: 9
                    text: pane.noteNames[key.pc] + (Math.floor(key.pitch / 12) - 1)
                }
            }
        }
    }

    PianoRollItem {
        id: view
        objectName: "pianoRoll"
        x: pane.keyWidth
        width: pane.width - x
        height: pane.height
        controller: pane.controller
        focus: true
    }
}
