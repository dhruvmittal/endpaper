import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.endpaper 1.0
import "Pages" as Pages

Kirigami.ApplicationWindow {
    id: root

    title: "Endpaper"
    width: Kirigami.Units.gridUnit * 55
    height: Kirigami.Units.gridUnit * 42
    visible: true

    function triggerPageOpen(idx) {
        if (idx === 0) Bridge.pageOpened("kmail")
        else if (idx === 1) Bridge.pageOpened("miniflux")
    }

    Component.onCompleted: {
        triggerPageOpen(sidebar.currentIndex)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            Layout.fillHeight: true
            color: Kirigami.Theme.alternateBackgroundColor

            ListView {
                id: sidebar
                anchors.fill: parent
                currentIndex: Bridge.lastPageIndex
                model: ListModel {
                    ListElement { name: "KMail Ingest"; iconName: "mail-folder-inbox" }
                    ListElement { name: "Miniflux Ingest"; iconName: "view-web-browser-dom-tree" }
                    ListElement { name: "RSS Ingest"; iconName: "rss-active" }
                    ListElement { name: "Books"; iconName: "address-book-new" }
                    ListElement { name: "Build EPUB"; iconName: "run-build" }
                    ListElement { name: "Settings"; iconName: "settings-configure" }
                }
                delegate: Controls.ItemDelegate {
                    width: ListView.view.width
                    text: model.name
                    icon.name: model.iconName
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        Bridge.lastPageIndex = index
                        root.triggerPageOpen(index)
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Controls.ProgressBar {
                Layout.fillWidth: true
                indeterminate: true
                visible: Bridge.isBusy
            }

            Kirigami.InlineMessage {
                id: statusBanner
                Layout.fillWidth: true
                Layout.margins: visible ? Kirigami.Units.smallSpacing : 0
                type: Bridge.statusIsError ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
                text: Bridge.statusMessage
                visible: Bridge.statusMessage !== ""
                showCloseButton: true
                onVisibleChanged: {
                    if (!visible && Bridge.statusMessage !== "") {
                        Bridge.clearStatus()
                    }
                }
                Connections {
                    target: Bridge
                    function onStatusMessageChanged() {
                        statusBanner.visible = Bridge.statusMessage !== ""
                    }
                }
            }

            StackLayout {
                id: stackLayout
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: sidebar.currentIndex

                Pages.IngestPage {}
                Pages.MinifluxPage {}
                Pages.RssPage {}
                Pages.BooksPage {}
                Pages.BuildPage {}
                Pages.SettingsPage {}
            }
        }
    }
}
