import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0
import "../Components" as Components

Kirigami.Page {
    id: root
    title: "KMail Ingest"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        anchors.fill: parent

        FormCard.FormHeader {
            title: "Akonadi Folders"
        }

        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                text: "Fetch Folders"
                icon.name: "view-refresh"
                enabled: !Bridge.isBusy
                onClicked: Bridge.fetchFolders()
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormComboBoxDelegate {
                id: folderComboBox
                text: "Folder"
                model: Bridge.folderModel
                textRole: "fullPath"
                valueRole: "collectionId"
                enabled: !Bridge.isBusy
                onActivated: {
                    if (currentValue !== undefined) {
                        Bridge.lastKMailCollectionId = currentValue
                        Bridge.fetchEmails(currentValue)
                    }
                }

                function syncSelection() {
                    const idx = model.indexOf(Bridge.lastKMailCollectionId)
                    if (idx !== -1 && currentIndex !== idx) {
                        currentIndex = idx
                    }
                }

                Component.onCompleted: syncSelection()
                Connections {
                    target: folderComboBox.model
                    function onCountChanged() { folderComboBox.syncSelection() }
                    function onModelReset() { folderComboBox.syncSelection() }
                }
                Connections {
                    target: Bridge
                    function onLastKMailCollectionIdChanged() { folderComboBox.syncSelection() }
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Fetch Emails"
                icon.name: "mail-folder-inbox"
                enabled: !Bridge.isBusy && folderComboBox.currentValue !== undefined
                onClicked: {
                    if (folderComboBox.currentValue !== undefined) {
                        Bridge.lastKMailCollectionId = folderComboBox.currentValue
                        Bridge.fetchEmails(folderComboBox.currentValue)
                    }
                }
            }
        }

        FormCard.FormHeader {
            title: "Emails to Stage"
        }

        Components.ArticleListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            filterModel: Bridge.emailFilterModel
            searchPlaceholder: "Filter emails..."
            emptyText: "Select a folder and click Fetch Emails"
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.emailFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.emailFilterModel.deselectAll()
            }
            Item { Layout.fillWidth: true }
            Controls.Label { text: "Book:" }
            Components.BookSelector {}
            Controls.Button {
                text: "Stage Selected (" + Bridge.emailFilterModel.selectedCount + ")"
                icon.name: "mail-forward"
                enabled: !Bridge.isBusy && Bridge.emailFilterModel.selectedCount > 0 && Bridge.selectedBookId !== -1
                onClicked: Bridge.stageSelectedEmails()
            }
        }
    }
}
