import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0
import "../Components" as Components

    Kirigami.Page {
    title: "KMail Ingest"

    Component.onCompleted: Bridge.pageOpened("kmail")

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        anchors.fill: parent

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: Bridge.statusMessage
            visible: Bridge.statusMessage !== ""
            showCloseButton: true
        }


        FormCard.FormHeader {
            title: "Akonadi Headers"
        }

        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                text: "Fetch Folders"
                icon.name: "view-refresh"
                onClicked: Bridge.fetchFolders()
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormComboBoxDelegate {
                id: folderComboBox
                text: "Collection"
                model: Bridge.folderModel
                textRole: "fullPath"
                valueRole: "collectionId"
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Fetch Emails"
                icon.name: "mail-folder-inbox"
                onClicked: {
                    if (folderComboBox.currentValue !== undefined) {
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
        }
        
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                onClicked: Bridge.emailFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                onClicked: Bridge.emailFilterModel.deselectAll()
            }
            Item { Layout.fillWidth: true }
            Controls.Label { text: "Book:" }
            Controls.ComboBox {
                model: Bridge.bookModel
                textRole: "title"
                valueRole: "id"
                currentIndex: Bridge.bookModel.indexOf(Bridge.selectedBookId)
                onActivated: Bridge.selectedBookId = currentValue
            }
            Controls.Button {
                text: "Stage Selected"
                icon.name: "mail-forward"
                onClicked: Bridge.stageSelectedEmails()
            }
        }
    }
}
