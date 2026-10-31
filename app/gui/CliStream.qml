import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

// Minimal QML host used for CLI / Ferry stream launches. Unlike main.qml, this
// never instantiates the PC list, app grid, settings page, or their icons and
// dialogs; it only provides the StackView + tool bar + error dialog that
// CliStartStreamSegue / StreamSegue need to start and run a stream.

ApplicationWindow {
    id: window
    width: 640
    height: 480

    function doEarlyInit() {
        if (SystemProperties.usesMaterial3Theme) {
            Material.background = "#303030"
        }
        SdlGamepadKeyNavigation.enable()
    }

    Component.onCompleted: {
        window.show()
    }

    Text {
        id: tooltipTextLayoutHelper
        visible: false
        font: ToolTip.toolTip.font
        text: ToolTip.toolTip.text
    }
    ToolTip.toolTip.contentWidth: Math.min(tooltipTextLayoutHelper.width, 400)

    StackView {
        id: stackView
        anchors.fill: parent
        focus: true

        Component.onCompleted: {
            doEarlyInit()
            push(initialView)
        }

        onCurrentItemChanged: {
            if (currentItem) {
                currentItem.forceActiveFocus()
            }
        }
    }

    // A minimal tool bar. CliStartStreamSegue / StreamSegue only toggle
    // `toolBar.visible`; they never depend on its buttons.
    header: ToolBar {
        id: toolBar
        height: 1
        visible: false
    }

    ErrorMessageDialog {
        id: streamSegueErrorDialog

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit()
            }
            text = ""
        }
    }
}
