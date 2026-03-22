import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.endpaper 1.0

Controls.ApplicationWindow {
    id: root

    title: "Endpaper"
    width: Kirigami.Units.gridUnit * 50
    height: Kirigami.Units.gridUnit * 40
    visible: true

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
                model: ListModel {
                    ListElement { name: "KMail Ingest"; iconName: "mail-folder-inbox"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/IngestPage.qml" }
                    ListElement { name: "Miniflux Ingest"; iconName: "view-web-browser-dom-tree"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/MinifluxPage.qml" }
                    ListElement { name: "RSS Ingest"; iconName: "rss-active"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/RssPage.qml" }
                    ListElement { name: "Books"; iconName: "address-book-new"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/BooksPage.qml" }
                    ListElement { name: "Build EPUB"; iconName: "run-build"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/BuildPage.qml" }
                    ListElement { name: "Settings"; iconName: "settings-configure"; pageUri: "qrc:/org/kde/endpaper/src/qml/Pages/SettingsPage.qml" }
                }
                delegate: Controls.ItemDelegate {
                    width: ListView.view.width
                    text: model.name
                    icon.name: model.iconName
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        sidebar.currentIndex = index
                        stack.replace(model.pageUri, {}, Controls.StackView.Immediate)
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        Controls.StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: "qrc:/org/kde/endpaper/src/qml/Pages/IngestPage.qml"
        }
    }
}
