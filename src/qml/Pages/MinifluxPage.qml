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
    
    property bool __ready: false
    Component.onCompleted: Bridge.pageOpened("miniflux")

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
            title: "Categories"
        }

        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                text: "Fetch Categories"
                icon.name: "view-refresh"
                onClicked: Bridge.fetchMinifluxCategories()
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormComboBoxDelegate {
                id: categoryComboBox
                text: "Category"
                model: Bridge.minifluxCategoryModel
                textRole: "title"
                valueRole: "id"
                onCurrentValueChanged: {
                    if (root.__ready && currentValue !== undefined) {
                        Bridge.lastMinifluxCategoryId = currentValue
                        Bridge.fetchMinifluxEntries(currentValue, Bridge.lastMinifluxStatusFilter)
                    }
                }

                function syncSelection() {
                    if (Bridge.lastMinifluxCategoryId !== -1 && model.count > 0) {
                        const idx = model.indexOf(Bridge.lastMinifluxCategoryId)
                        if (idx !== -1 && currentIndex !== idx) {
                            currentIndex = idx
                        }
                    }
                    if (model.count > 0) readyTimer.start()
                }

                Component.onCompleted: syncSelection()
                Connections {
                    target: categoryComboBox.model
                    function onCountChanged() { categoryComboBox.syncSelection() }
                }
                Timer {
                    id: readyTimer
                    interval: 100
                    onTriggered: root.__ready = true
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
                    Controls.RadioButton {
                        text: "Unread"
                        checked: Bridge.lastMinifluxStatusFilter === 0
                        onClicked: {
                            Bridge.lastMinifluxStatusFilter = 0
                            Bridge.fetchMinifluxEntries(categoryComboBox.currentValue, 0)
                        }
                    }
                    Controls.RadioButton {
                        text: "Starred"
                        checked: Bridge.lastMinifluxStatusFilter === 1
                        onClicked: {
                            Bridge.lastMinifluxStatusFilter = 1
                            Bridge.fetchMinifluxEntries(categoryComboBox.currentValue, 1)
                        }
                    }
                    Controls.RadioButton {
                        text: "All"
                        checked: Bridge.lastMinifluxStatusFilter === 2
                        onClicked: {
                            Bridge.lastMinifluxStatusFilter = 2
                            Bridge.fetchMinifluxEntries(categoryComboBox.currentValue, 2)
                        }
                    }
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Fetch Entries"
                icon.name: "download"
                onClicked: {
                    let catId = categoryComboBox.currentValue !== undefined ? categoryComboBox.currentValue : -1;
                    Bridge.fetchMinifluxEntries(catId, Bridge.lastMinifluxStatusFilter);
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
            searchPlaceholder: "Filter RSS entries..."
        }
        
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Select All"
                onClicked: Bridge.minifluxFilterModel.selectAll()
            }
            Controls.Button {
                text: "Deselect All"
                onClicked: Bridge.minifluxFilterModel.deselectAll()
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
                onClicked: Bridge.stageSelectedMiniflux()
            }
        }
    }
}
