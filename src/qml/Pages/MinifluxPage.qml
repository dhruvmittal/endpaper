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
    
    Component.onCompleted: Bridge.pageOpened("miniflux")
    
    property int statusIndex: 0

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
                        checked: true
                        onClicked: root.statusIndex = 0
                    }
                    Controls.RadioButton {
                        text: "Starred"
                        onClicked: root.statusIndex = 1
                    }
                    Controls.RadioButton {
                        text: "All"
                        onClicked: root.statusIndex = 2
                    }
                }
            }
            
            FormCard.FormDelegateSeparator {}
            
            FormCard.FormButtonDelegate {
                text: "Fetch Entries"
                icon.name: "download"
                onClicked: {
                    let catId = categoryComboBox.currentValue !== undefined ? categoryComboBox.currentValue : -1;
                    Bridge.fetchMinifluxEntries(catId, root.statusIndex);
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
