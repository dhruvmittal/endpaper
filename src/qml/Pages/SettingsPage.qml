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
            title: "Automation & Caching"
        }

        FormCard.FormCard {
            FormCard.FormSwitchDelegate {
                text: "Auto-fetch KMail Folders"
                description: "Fetch folders automatically when KMail tab is opened"
                checked: Bridge.autoFetchKMail
                onToggled: Bridge.autoFetchKMail = checked
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormSwitchDelegate {
                text: "Auto-fetch Miniflux Categories"
                description: "Fetch categories automatically when Miniflux tab is opened"
                checked: Bridge.autoFetchMiniflux
                onToggled: Bridge.autoFetchMiniflux = checked
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormSwitchDelegate {
                text: "Cache Metadata"
                description: "Persist folders and categories between sessions for instant loading"
                checked: Bridge.cacheMetadata
                onToggled: Bridge.cacheMetadata = checked
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormSwitchDelegate {
                text: "Remember Last Application State"
                description: "Restore previous focus, category, and filter selections on startup"
                checked: Bridge.rememberState
                onToggled: Bridge.rememberState = checked
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
