import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.ScrollablePage {
    title: "Books Management"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        width: parent.width

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: Bridge.statusMessage
            visible: Bridge.statusMessage !== ""
            showCloseButton: true
        }

        FormCard.FormHeader {
            title: "Books"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                id: newBookInput
                label: "Title"
                text: "New Book"
            }
            FormCard.FormButtonDelegate {
                text: "Create Book"
                icon.name: "document-new"
                onClicked: {
                    Bridge.createBook(newBookInput.text)
                    newBookInput.text = "New Book"
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormComboBoxDelegate {
                id: bookComboBox
                text: "Select Book"
                model: Bridge.bookModel
                textRole: "title"
                valueRole: "id"
                currentIndex: Bridge.bookModel.indexOf(Bridge.selectedBookId)
                onCurrentValueChanged: {
                    if (currentValue !== undefined) {
                        Bridge.selectedBookId = currentValue
                    }
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Delete Selected Book"
                icon.name: "edit-delete"
                onClicked: {
                    if (bookComboBox.currentValue !== undefined) {
                        Bridge.deleteBook(bookComboBox.currentValue)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Kirigami.Units.largeSpacing
            
            FormCard.FormHeader {
                title: "Chapters in Book"
                Layout.fillWidth: true
            }
            
            Controls.ToolButton {
                icon.name: "media-playlist-reverse"
                onClicked: Bridge.reverseChapterOrder()
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: "Reverse all chapters"
            }
        }

        ListView {
            id: chapterListView
            model: Bridge.chapterModel
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 15
            clip: true
            
            delegate: Controls.ItemDelegate {
                width: ListView.view.width
                text: model.subject + " - " + model.sender

                RowLayout {
                    anchors.right: parent.right
                    anchors.rightMargin: Kirigami.Units.largeSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    
                    Controls.ToolButton {
                        icon.name: "go-up"
                        onClicked: Bridge.moveChapterUp(model.id)
                    }
                    Controls.ToolButton {
                        icon.name: "go-down"
                        onClicked: Bridge.moveChapterDown(model.id)
                    }
                    Controls.ToolButton {
                        icon.name: "edit-delete"
                        onClicked: Bridge.deleteChapter(model.id)
                    }
                }
            }
        }
    }
}
