import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0

Item {
    function onSearchingComputer() {
        // Copy stays fixed: primary "正在启动 Desktop…", secondary below.
    }

    function onSearchingApp() {
    }

    function onSessionCreated(appName, session) {
        var component = Qt.createComponent("StreamSegue.qml")
        var segue = component.createObject(stackView, {
            "appName": appName,
            "session": session,
            "quitAfter": true
        })
        stackView.push(segue, {}, StackView.Immediate)
    }

    function onLaunchFailed(message) {
        // Ferry mode reports an empty message when it hands control back to
        // Ferry for automatic re-pairing; quit silently in that case instead
        // of showing an empty (localized) dialog.
        if (message.length === 0) {
            Qt.quit()
            return
        }
        errorDialog.text = message
        errorDialog.open()
        console.error(message)
    }

    function onAppQuitRequired(appName) {
        quitAppDialog.appName = appName
        quitAppDialog.open()
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            toolBar.visible = false

            launcher.searchingComputer.connect(onSearchingComputer)
            launcher.searchingApp.connect(onSearchingApp)
            launcher.sessionCreated.connect(onSessionCreated)
            launcher.failed.connect(onLaunchFailed)
            launcher.appQuitRequired.connect(onAppQuitRequired)
            launcher.execute(ComputerManager)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#1E1E1E"
    }

    Item {
        anchors.centerIn: parent
        width: Math.max(stageSpinner.implicitWidth, stageLabel.implicitWidth, loadingHint.implicitWidth)
        height: stageSpinner.implicitHeight + 18 + stageLabel.implicitHeight + 8 + loadingHint.implicitHeight

        BusyIndicator {
            id: stageSpinner
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            running: visible
            Material.accent: "#6CB6FF"
        }

        Label {
            id: stageLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: stageSpinner.bottom
            anchors.topMargin: 18
            text: "正在启动 Desktop…"
            color: "#E6E6E6"
            font.pointSize: 20
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            id: loadingHint
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: stageLabel.bottom
            anchors.topMargin: 8
            text: "应用程序列表加载中"
            color: "#8C8C8C"
            font.pointSize: 14
            horizontalAlignment: Text.AlignHCenter
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit();
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No
        property string appName : ""

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { launcher.quitRunningApp() }}
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
        onRejected: Qt.quit()
    }
}
