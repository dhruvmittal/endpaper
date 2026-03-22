import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0
import "../Components" as Components

Kirigami.Page {
    title: "RSS Feed Ingest"

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
            title: "Manage Feeds"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                id: feedTitleInput
                label: "Title"
                text: "New Feed"
            }
            FormCard.FormTextFieldDelegate {
                id: feedUrlInput
                label: "URL"
                text: "https://"
            }
            FormCard.FormButtonDelegate {
                text: "Add Feed"
                icon.name: "list-add"
                onClicked: {
                    Bridge.addRssFeed(feedTitleInput.text, feedUrlInput.text)
                    feedTitleInput.text = "New Feed"
                    feedUrlInput.text = "https://"
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormComboBoxDelegate {
                id: feedComboBox
                text: "Select Feed"
                model: Bridge.rssFeedModel
                textRole: "title"
                valueRole: "id"
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Remove Selected Feed"
                icon.name: "list-remove"
                onClicked: {
                    if (feedComboBox.currentValue !== undefined) {
                        Bridge.removeRssFeed(feedComboBox.currentValue)
                    }
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Fetch Entries"
                icon.name: "download"
                onClicked: {
                    if (feedComboBox.currentValue !== undefined) {
                        Bridge.fetchRssEntries(feedComboBox.currentValue);
                    }
                }
            }
        }

        FormCard.FormHeader {
            title: "Entries to Stage"
        }

        Components.ArticleListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            filterModel: Bridge.rssFilterModel
            searchPlaceholder: "Filter RSS entries..."
        }
        
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                onClicked: Bridge.rssFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                onClicked: Bridge.rssFilterModel.deselectAll()
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
                onClicked: Bridge.stageSelectedRss()
            }
        }
    }
}
