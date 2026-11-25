import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.endpaper 1.0
import "../Components" as Components

Kirigami.Page {
    id: root
    title: "Miniflux Ingest"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        anchors.fill: parent

        FormCard.FormHeader {
            title: "Categories"
        }

        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                text: "Fetch Categories"
                icon.name: "view-refresh"
                enabled: !Bridge.isBusy
                onClicked: Bridge.fetchMinifluxCategories()
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormComboBoxDelegate {
                id: categoryComboBox
                text: "Category"
                model: Bridge.minifluxCategoryModel
                textRole: "displayTitle"
                valueRole: "id"
                enabled: !Bridge.isBusy
                onActivated: {
                    if (currentValue !== undefined) {
                        Bridge.lastMinifluxCategoryId = currentValue
                        Bridge.fetchMinifluxEntries(currentValue, Bridge.lastMinifluxStatusFilter)
                    }
                }

                function syncSelection() {
                    const idx = model.indexOf(Bridge.lastMinifluxCategoryId)
                    if (idx !== -1 && currentIndex !== idx) {
                        currentIndex = idx
                    }
                }

                Component.onCompleted: syncSelection()
                Connections {
                    target: categoryComboBox.model
                    function onCountChanged() { categoryComboBox.syncSelection() }
                    function onModelReset() { categoryComboBox.syncSelection() }
                }
                Connections {
                    target: Bridge
                    function onLastMinifluxCategoryIdChanged() { categoryComboBox.syncSelection() }
                }
            }

            FormCard.FormDelegateSeparator {}

            Item {
                Layout.fillWidth: true
                implicitHeight: Kirigami.Units.gridUnit * 2
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Kirigami.Units.largeSpacing
                    anchors.rightMargin: Kirigami.Units.largeSpacing
                    Controls.Label {
                        text: "Status Filter"
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: ["Unread", "Starred", "All"]
                        delegate: Controls.RadioButton {
                            required property int index
                            required property string modelData
                            text: modelData
                            enabled: !Bridge.isBusy
                            checked: Bridge.lastMinifluxStatusFilter === index
                            onClicked: {
                                checked = Qt.binding(function() { return Bridge.lastMinifluxStatusFilter === index })
                                Bridge.lastMinifluxStatusFilter = index
                                let catId = categoryComboBox.currentValue !== undefined ? categoryComboBox.currentValue : -1
                                Bridge.fetchMinifluxEntries(catId, index)
                            }
                        }
                    }
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: "Fetch Entries"
                icon.name: "download"
                enabled: !Bridge.isBusy
                onClicked: {
                    let catId = categoryComboBox.currentValue !== undefined ? categoryComboBox.currentValue : -1
                    Bridge.fetchMinifluxEntries(catId, Bridge.lastMinifluxStatusFilter)
                }
            }
        }

        FormCard.FormHeader {
            title: "Entries to Stage"
        }

        Components.ArticleListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            filterModel: Bridge.minifluxFilterModel
            searchPlaceholder: "Filter Miniflux entries..."
            emptyText: "Click Fetch Entries to load articles from Miniflux"
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.minifluxFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                enabled: !Bridge.isBusy
                onClicked: Bridge.minifluxFilterModel.deselectAll()
            }
            Item { Layout.fillWidth: true }
            Controls.Label { text: "Book:" }
            Components.BookSelector {}
            Controls.Button {
                text: "Stage Selected (" + Bridge.minifluxFilterModel.selectedCount + ")"
                icon.name: "mail-forward"
                enabled: !Bridge.isBusy && Bridge.minifluxFilterModel.selectedCount > 0 && Bridge.selectedBookId !== -1
                onClicked: Bridge.stageSelectedMiniflux()
            }
        }
    }
}
