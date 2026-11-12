pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lina.Spike

ApplicationWindow {
    id: root

    required property EditController controller

    width: 760
    height: 440
    visible: true
    title: "LINA spike: Tracktion Engine + Qt Quick"

    function formatTime(seconds: real): string {
        const m = Math.floor(seconds / 60)
        const s = seconds - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(3)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Button {
                objectName: "playButton"
                text: "Play"
                enabled: !root.controller.playing
                onClicked: root.controller.play()
            }
            Button {
                objectName: "stopButton"
                text: "Stop"
                onClicked: root.controller.stop()
            }
            Label { text: "Tempo" }
            TextField {
                id: tempoField
                objectName: "tempoField"
                Layout.preferredWidth: 80
                selectByMouse: true
                validator: DoubleValidator { bottom: 20; top: 300; decimals: 2; notation: DoubleValidator.StandardNotation }
                Component.onCompleted: text = root.controller.tempo.toFixed(2)
                onEditingFinished: {
                    root.controller.tempo = Number(text)
                    text = root.controller.tempo.toFixed(2)
                }
                Connections {
                    target: root.controller
                    function onTempoChanged() { tempoField.text = root.controller.tempo.toFixed(2) }
                }
            }
            Label { text: "bpm" }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "positionLabel"
                font.family: "monospace"
                font.pixelSize: 20
                text: root.formatTime(root.controller.position)
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.controller.canUndo ? "Undo " + root.controller.undoText : "Undo"
                enabled: root.controller.canUndo
                onClicked: root.controller.undo()
            }
            Button {
                text: "Redo"
                enabled: root.controller.canRedo
                onClicked: root.controller.redo()
            }
        }
    }

    ListView {
        id: trackList
        objectName: "trackList"
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10
        clip: true
        model: root.controller.tracks

        delegate: Column {
            id: trackDelegate

            required property int index
            required property string name
            required property ClipListModel clips

            width: trackList.width
            spacing: 4

            Label {
                text: "Track " + (trackDelegate.index + 1) + ": " + trackDelegate.name
                font.bold: true
            }

            Repeater {
                model: trackDelegate.clips

                delegate: RowLayout {
                    id: clipDelegate

                    required property int index
                    required property string name
                    required property string kind
                    required property double start
                    required property double length

                    width: trackDelegate.width
                    spacing: 8

                    Label {
                        text: clipDelegate.kind
                        Layout.preferredWidth: 90
                        Layout.leftMargin: 16
                    }
                    TextField {
                        objectName: "clipName_" + trackDelegate.index + "_" + clipDelegate.index
                        Layout.preferredWidth: 240
                        selectByMouse: true
                        text: clipDelegate.name
                        onEditingFinished: {
                            trackDelegate.clips.rename(clipDelegate.index, text)
                            // Typing broke the binding; restore it so undo and engine-side
                            // renames show up here again.
                            text = Qt.binding(() => clipDelegate.name)
                        }
                    }
                    Label {
                        text: clipDelegate.start.toFixed(2) + " s, length " + clipDelegate.length.toFixed(2) + " s"
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
