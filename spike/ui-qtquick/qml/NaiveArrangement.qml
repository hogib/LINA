// SPDX-License-Identifier: GPL-3.0-or-later
// NAIVE VARIANT, for measurement only: one QML Rectangle (+ Text) per clip, created by a
// Repeater over a flat list model. No waveforms, no editing gestures, no culling.
// Scroll moves one container item; zoom re-evaluates every delegate's bindings.
import QtQuick
import Lina

Item {
    id: naive

    required property AppController controller
    property real pxPerTick: 0.02
    property real scrollTick: 0
    property real scrollY: 0
    property real trackH: 56
    readonly property int count: repeater.count

    clip: true

    ClipListModel {
        id: clips
        controller: naive.controller
    }

    Rectangle {
        anchors.fill: parent
        color: "#1a1a1a"
    }

    Item {
        x: -naive.scrollTick * naive.pxPerTick
        y: -naive.scrollY

        Repeater {
            id: repeater
            model: clips
            delegate: Rectangle {
                id: clipRect
                required property int track
                required property real start
                required property real len
                required property string clipName
                required property color clipColor

                x: start * naive.pxPerTick
                y: track * naive.trackH + 1
                width: Math.max(1, len * naive.pxPerTick)
                height: naive.trackH - 2
                color: clipColor

                Text {
                    visible: clipRect.width >= 34 && naive.trackH >= 26
                    x: 4
                    y: 2
                    width: clipRect.width - 8
                    elide: Text.ElideRight
                    color: "#ebebeb"
                    font.pixelSize: 10
                    font.family: "monospace"
                    text: clipRect.clipName
                }
            }
        }
    }

    Rectangle {
        x: (naive.controller.playheadTick - naive.scrollTick) * naive.pxPerTick
        width: 1
        height: naive.height
        color: "#ff9628"
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (ev) => {
            if (ev.modifiers & Qt.ControlModifier) {
                const anchor = naive.scrollTick + point.position.x / naive.pxPerTick
                naive.pxPerTick = Math.min(1, Math.max(0.0005, naive.pxPerTick * Math.exp(ev.angleDelta.y / 600)))
                naive.scrollTick = Math.max(0, anchor - point.position.x / naive.pxPerTick)
            } else {
                naive.scrollTick = Math.max(0, naive.scrollTick - ev.angleDelta.x / 2.5 / naive.pxPerTick)
                naive.scrollY = Math.max(0, naive.scrollY - ev.angleDelta.y / 2.5)
            }
        }
    }
}
