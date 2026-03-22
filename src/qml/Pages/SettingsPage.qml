import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.ScrollablePage {
    title: "Settings"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        width: parent.width

        FormCard.FormHeader {
            title: "General"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                id: authorField
                label: "Default Author"
                text: "Unknown"
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                id: publisherField
                label: "Default Publisher"
                text: "Endpaper"
            }
        }

        FormCard.FormHeader {
            title: "System"
        }

        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                text: "Save Configuration"
                icon.name: "document-save"
                onClicked: {
                    console.log("Saving configuration...")
                    // Can be connected to Bridge if exposed, or just to show the UI
                }
            }
        }
    }
}
