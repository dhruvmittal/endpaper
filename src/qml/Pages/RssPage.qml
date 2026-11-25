import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0
import "../Components" as Components

Kirigami.Page {
    id: root
    title: "RSS Feed Ingest"

    Kirigami.PromptDialog {
        id: removeFeedDialog
        title: "Remove RSS Feed"
        subtitle: "Remove \"" + feedComboBox.currentText + "\" from your saved feeds?"
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            if (feedComboBox.currentValue !== undefined) {
                Bridge.removeRssFeed(feedComboBox.currentValue)
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        anchors.fill: parent

        FormCard.FormHeader {
            title: "Manage Feeds"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                id: feedTitleInput
                label: "Title"
                placeholderText: "My Blog Feed"
                text: ""
            }
            FormCard.FormTextFieldDelegate {
                id: feedUrlInput
                label: "URL"
                placeholderText: "https://example.com/feed.xml"
                text: ""
                onAccepted: {
                    if (!Bridge.isBusy && feedTitleInput.text.trim() !== "" && feedUrlInput.text.trim() !== "") {
                        Bridge.addRssFeed(feedTitleInput.text, feedUrlInput.text)
                        feedTitleInput.text = ""
                        feedUrlInput.text = ""
                    }
                }
            }
            FormCard.FormButtonDelegate {
                text: "Add Feed"
                icon.name: "list-add"
                enabled: !Bridge.isBusy && feedTitleInput.text.trim() !== "" && feedUrlInput.text.trim() !== ""
                onClicked: {
                    Bridge.addRssFeed(feedTitleInput.text, feedUrlInput.text)
                    feedTitleInput.text = ""
                    feedUrlInput.text = ""
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormComboBoxDelegate {
                id: feedComboBox
                text: "Feed"
                model: Bridge.rssFeedModel
                textRole: "title"
                valueRole: "id"
                enabled: !Bridge.isBusy
                onActivated: {
                    if (currentValue !== undefined) {
                        Bridge.lastRssFeedId = currentValue
                        Bridge.fetchRssEntries(currentValue)
                    }
                }

                function syncSelection() {
                    const idx = model.indexOf(Bridge.lastRssFeedId)
                    if (idx !== -1 && currentIndex !== idx) {
                        currentIndex = idx
                    }
                }

                Component.onCompleted: syncSelection()
                Connections {
                    target: feedComboBox.model
                    function onCountChanged() { feedComboBox.syncSelection() }
                    function onModelReset() { feedComboBox.syncSelection() }
                }
                Connections {
                    target: Bridge
                    function onLastRssFeedIdChanged() { feedComboBox.syncSelection() }
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Remove Selected Feed"
                icon.name: "list-remove"
                enabled: !Bridge.isBusy && feedComboBox.currentValue !== undefined
                onClicked: removeFeedDialog.open()
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Fetch Entries"
                icon.name: "download"
                enabled: !Bridge.isBusy && feedComboBox.currentValue !== undefined
                onClicked: {
                    if (feedComboBox.currentValue !== undefined) {
                        Bridge.lastRssFeedId = feedComboBox.currentValue
                        Bridge.fetchRssEntries(feedComboBox.currentValue)
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
            emptyText: "Select a feed and click Fetch Entries"
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.rssFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.rssFilterModel.deselectAll()
            }
            Item { Layout.fillWidth: true }
            Controls.Label { text: "Book:" }
            Components.BookSelector {}
            Controls.Button {
                text: "Stage Selected (" + Bridge.rssFilterModel.selectedCount + ")"
                icon.name: "mail-forward"
                enabled: !Bridge.isBusy && Bridge.rssFilterModel.selectedCount > 0 && Bridge.selectedBookId !== -1
                onClicked: Bridge.stageSelectedRss()
            }
        }
    }
}
