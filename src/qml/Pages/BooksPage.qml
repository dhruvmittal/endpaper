import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.Page {
    id: root
    title: "Books Management"

    Kirigami.PromptDialog {
        id: deleteBookDialog
        title: "Delete Book"
        subtitle: "Delete \"" + Bridge.selectedBookTitle + "\" and all of its staged chapters?"
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            if (Bridge.selectedBookId !== -1) {
                Bridge.deleteBook(Bridge.selectedBookId)
            }
        }
    }

    Kirigami.PromptDialog {
        id: clearChaptersDialog
        title: "Clear All Chapters"
        subtitle: "Remove every staged chapter from \"" + Bridge.selectedBookTitle + "\"?"
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: Bridge.clearChapters()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        anchors.fill: parent

        FormCard.FormHeader {
            title: "Books"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                id: newBookInput
                label: "Title"
                placeholderText: "Enter new book title..."
                text: ""
                onAccepted: {
                    if (!Bridge.isBusy && newBookInput.text.trim() !== "") {
                        Bridge.createBook(newBookInput.text)
                        newBookInput.text = ""
                    }
                }
            }
            FormCard.FormButtonDelegate {
                text: "Create Book"
                icon.name: "document-new"
                enabled: !Bridge.isBusy && newBookInput.text.trim() !== ""
                onClicked: {
                    Bridge.createBook(newBookInput.text)
                    newBookInput.text = ""
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormComboBoxDelegate {
                id: bookComboBox
                text: "Select Book"
                model: Bridge.bookModel
                textRole: "displayTitle"
                valueRole: "id"
                enabled: !Bridge.isBusy

                function syncIndex() {
                    const idx = Bridge.bookModel.indexOf(Bridge.selectedBookId)
                    if (currentIndex !== idx) {
                        currentIndex = idx
                    }
                }

                Component.onCompleted: syncIndex()
                onActivated: {
                    if (currentValue !== undefined) {
                        Bridge.selectedBookId = currentValue
                    }
                }
                Connections {
                    target: Bridge
                    function onSelectedBookIdChanged() { bookComboBox.syncIndex() }
                }
                Connections {
                    target: Bridge.bookModel
                    function onModelReset() { bookComboBox.syncIndex() }
                    function onCountChanged() { bookComboBox.syncIndex() }
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Delete Selected Book"
                icon.name: "edit-delete"
                enabled: !Bridge.isBusy && Bridge.selectedBookId !== -1
                onClicked: deleteBookDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Kirigami.Units.largeSpacing

            FormCard.FormHeader {
                title: "Chapters in Book (" + Bridge.chapterModel.count + ")"
                Layout.fillWidth: true
            }

            Controls.ToolButton {
                icon.name: "view-sort-ascending"
                enabled: !Bridge.isBusy && Bridge.chapterModel.count > 1
                onClicked: Bridge.autoSortChapters()
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Auto-sort chapters by number/title"
            }

            Controls.ToolButton {
                icon.name: "media-playlist-reverse"
                enabled: !Bridge.isBusy && Bridge.chapterModel.count > 1
                onClicked: Bridge.reverseChapterOrder()
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Reverse all chapters"
            }

            Controls.ToolButton {
                icon.name: "edit-clear-all"
                enabled: !Bridge.isBusy && Bridge.chapterModel.count > 0
                onClicked: clearChaptersDialog.open()
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Clear all chapters"
            }
        }

        ListView {
            id: chapterListView
            model: Bridge.chapterModel
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - (Kirigami.Units.largeSpacing * 4)
                visible: chapterListView.count === 0
                text: Bridge.selectedBookId === -1 ? "Create or select a book above" : "No chapters staged in this book"
            }

            delegate: Controls.ItemDelegate {
                width: ListView.view.width

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Controls.Label {
                        Layout.fillWidth: true
                        text: model.sortOrder + ". " + model.subject + (model.sender !== "" ? " — " + model.sender : "")
                        elide: Text.ElideRight
                    }

                    Controls.ToolButton {
                        icon.name: "go-up"
                        enabled: !Bridge.isBusy && index > 0
                        onClicked: Bridge.moveChapterUp(model.id)
                    }
                    Controls.ToolButton {
                        icon.name: "go-down"
                        enabled: !Bridge.isBusy && index < chapterListView.count - 1
                        onClicked: Bridge.moveChapterDown(model.id)
                    }
                    Controls.ToolButton {
                        icon.name: "edit-delete"
                        enabled: !Bridge.isBusy
                        onClicked: Bridge.deleteChapter(model.id)
                    }
                }
            }
        }
    }
}
