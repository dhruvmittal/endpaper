import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.ScrollablePage {
    title: "Build EPUB"

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
            title: "Configuration"
        }

        FormCard.FormCard {
            FormCard.FormComboBoxDelegate {
                id: bookComboBox
                text: "Target Book"
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
            
            FormCard.FormTextFieldDelegate {
                id: authorInput
                label: "Author"
                text: "Unknown"
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormTextFieldDelegate {
                id: publisherInput
                label: "Publisher"
                text: "Endpaper"
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormTextFieldDelegate {
                id: pathInput
                label: "Output Path (optional)"
                text: ""
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormSwitchDelegate {
                id: appendSwitch
                text: "Append to existing file"
                checked: false
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormSwitchDelegate {
                id: imagesSwitch
                text: "Images"
                checked: false
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormSwitchDelegate {
                id: simplifySwitch
                text: "Simplify HTML"
                checked: true
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Build EPUB"
                icon.name: "run-build"
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
