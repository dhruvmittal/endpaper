import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.ScrollablePage {
    title: "Build EPUB"

    FolderDialog {
        id: folderDialog
        title: "Select Output Folder"
        onAccepted: {
            pathInput.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        width: parent.width

        FormCard.FormHeader {
            title: "Configuration"
        }

        FormCard.FormCard {
            FormCard.FormComboBoxDelegate {
                id: bookComboBox
                text: "Target Book"
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

            FormCard.FormTextFieldDelegate {
                id: authorInput
                label: "Author"
                text: Bridge.defaultAuthor
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormTextFieldDelegate {
                id: publisherInput
                label: "Publisher"
                text: Bridge.defaultPublisher
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormTextFieldDelegate {
                id: pathInput
                label: "Output Path (optional)"
                placeholderText: Bridge.resolvedOutputPath
                text: ""
            }

            FormCard.FormButtonDelegate {
                text: "Choose Output Folder..."
                icon.name: "folder-open"
                onClicked: folderDialog.open()
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormSwitchDelegate {
                id: appendSwitch
                text: "Append to existing file"
                checked: Bridge.buildAppend
                onToggled: {
                    const val = checked
                    checked = Qt.binding(function() { return Bridge.buildAppend })
                    Bridge.buildAppend = val
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormSwitchDelegate {
                id: imagesSwitch
                text: "Include Images (optimized grayscale for e-ink)"
                checked: Bridge.buildKeepImages
                onToggled: {
                    const val = checked
                    checked = Qt.binding(function() { return Bridge.buildKeepImages })
                    Bridge.buildKeepImages = val
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormSwitchDelegate {
                id: simplifySwitch
                text: "Simplify HTML"
                checked: Bridge.buildSimplify
                onToggled: {
                    const val = checked
                    checked = Qt.binding(function() { return Bridge.buildSimplify })
                    Bridge.buildSimplify = val
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Build EPUB"
                icon.name: "run-build"
                enabled: !Bridge.isBusy && Bridge.selectedBookId !== -1 && Bridge.chapterModel.count > 0
                onClicked: {
                    Bridge.buildEpub(
                        authorInput.text,
                        publisherInput.text,
                        pathInput.text,
                        appendSwitch.checked,
                        simplifySwitch.checked,
                        imagesSwitch.checked
                    )
                }
            }
        }
    }
}
