import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0

Kirigami.ScrollablePage {
    title: "Settings"

    FolderDialog {
        id: defaultFolderDialog
        title: "Select Default Output Folder"
        onAccepted: {
            const p = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
            Bridge.defaultOutputFolder = p
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        width: parent.width

        FormCard.FormHeader {
            title: "Miniflux API"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                label: "Server URL"
                placeholderText: "https://miniflux.example.com"
                text: Bridge.minifluxUrl
                onEditingFinished: Bridge.minifluxUrl = text
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormPasswordFieldDelegate {
                label: "API Token"
                placeholderText: "Miniflux API token"
                text: Bridge.minifluxToken
                onEditingFinished: Bridge.minifluxToken = text
            }
        }

        FormCard.FormHeader {
            title: "General & Build Defaults"
        }

        FormCard.FormCard {
            FormCard.FormTextFieldDelegate {
                label: "Default Author"
                text: Bridge.defaultAuthor
                onEditingFinished: Bridge.defaultAuthor = text
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                label: "Default Publisher"
                text: Bridge.defaultPublisher
                onEditingFinished: Bridge.defaultPublisher = text
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                label: "Default Output Folder"
                placeholderText: "Default (Documents)"
                text: Bridge.defaultOutputFolder
                onEditingFinished: Bridge.defaultOutputFolder = text
            }
            FormCard.FormButtonDelegate {
                text: "Browse Default Folder..."
                icon.name: "folder-open"
                onClicked: defaultFolderDialog.open()
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormSwitchDelegate {
                text: "Fuzzy Article Search"
                description: "Match space-separated words anywhere; disable for regular expressions"
                checked: Bridge.fuzzySearch
                onToggled: Bridge.fuzzySearch = checked
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                label: "RSS Max Articles (0 = unlimited)"
                text: Bridge.rssMaxArticles.toString()
                validator: IntValidator { bottom: 0; top: 10000 }
                onEditingFinished: {
                    const v = parseInt(text)
                    if (!isNaN(v)) Bridge.rssMaxArticles = v
                }
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
                description: "Restore previous tab, folder, category, book, and filter selections on startup"
                checked: Bridge.rememberState
                onToggled: Bridge.rememberState = checked
            }
        }

        FormCard.FormHeader {
            title: "Fetching & Stubs"
        }

        FormCard.FormCard {
            FormCard.FormSwitchDelegate {
                text: "Resolve Article Stubs"
                description: "Automatically fetch full content for short articles during staging"
                checked: Bridge.resolveStubs
                onToggled: Bridge.resolveStubs = checked
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                label: "Stub Threshold (characters)"
                text: Bridge.stubThreshold.toString()
                validator: IntValidator { bottom: 0; top: 1000000 }
                onEditingFinished: {
                    const v = parseInt(text)
                    if (!isNaN(v)) Bridge.stubThreshold = v
                }
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextFieldDelegate {
                label: "Request Delay (ms)"
                text: Bridge.requestDelayMs.toString()
                validator: IntValidator { bottom: 0; top: 60000 }
                onEditingFinished: {
                    const v = parseInt(text)
                    if (!isNaN(v)) Bridge.requestDelayMs = v
                }
            }
        }

        FormCard.FormHeader {
            title: "System"
        }

        FormCard.FormCard {
            FormCard.FormTextDelegate {
                text: "Akonadi Status"
                description: Bridge.akonadiAvailable ? "Connected" : "Disconnected (start with: akonadictl start)"
            }
            FormCard.FormButtonDelegate {
                text: "Refresh Akonadi Status"
                icon.name: "view-refresh"
                onClicked: Bridge.refreshAkonadiStatus()
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormTextDelegate {
                text: "Configuration File"
                description: Bridge.configFilePath
            }
        }
    }
}
