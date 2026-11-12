// SPDX-License-Identifier: GPL-3.0-or-later
// Root of the spike UI. An Item, not a Window, so the same scene loads into a QQuickView
// (windowed app) and into a QQuickRenderControl-driven offscreen window (bench).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina

Item {
    id: root

    required property AppController controller
    property int tab: 0
    // Bench knob: drop the QML track headers / keyboard column to isolate their cost.
    property bool showHeaders: true
    // Bench knob: hide header / key delegates that are scrolled out (the obvious QML
    // idiom, and a measurable mistake next to a big custom item).
    property bool cullDelegates: false
    property alias naiveLoader: naiveLoader

    Rectangle {
        anchors.fill: parent
        color: "#1a1a1a"
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 36
        color: "#2c2c2c"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Repeater {
                model: ["Arrangement", "Piano roll", "Naive: one QML item per clip"]
                delegate: Button {
                    required property string modelData
                    required property int index
                    text: modelData
                    highlighted: root.tab === index
                    focusPolicy: Qt.NoFocus
                    onClicked: root.tab = index
                }
            }
            Button {
                objectName: "playButton"
                text: root.controller.playing ? "Stop" : "Play"
                focusPolicy: Qt.NoFocus
                onClicked: root.controller.togglePlay()
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: "#bebebe"
                text: root.controller.applied + " commands applied; last: " + root.controller.lastCommand
            }
        }
    }

    ArrangementPane {
        id: arrPane
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: root.tab === 0
        controller: root.controller
        showHeaders: root.showHeaders
        cullDelegates: root.cullDelegates
    }

    PianoRollPane {
        id: prPane
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: root.tab === 1
        controller: root.controller
        showKeys: root.showHeaders
        cullDelegates: root.cullDelegates
    }

    Loader {
        id: naiveLoader
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        active: root.tab === 2
        sourceComponent: NaiveArrangement {
            controller: root.controller
        }
    }
}
