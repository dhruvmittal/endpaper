import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    
    property var filterModel
    property string searchPlaceholder: "Filter..."
    
    Kirigami.SearchField {
        Layout.fillWidth: true
        placeholderText: root.searchPlaceholder
        onTextChanged: {
            if (root.filterModel) {
                root.filterModel.filterString = text
            }
        }
    }

    ListView {
        id: listView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        
        model: root.filterModel
        
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
        
        delegate: Controls.ItemDelegate {
            id: delegateItem
            width: ListView.view.width
            
            topPadding: Kirigami.Units.smallSpacing
            bottomPadding: Kirigami.Units.smallSpacing

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                
                Controls.CheckBox {
                    checked: model.selected
                    // Consume clicks so it correctly bubbles down or blocks native delegate states?
                    // Actually, letting ItemDelegate trap the click is cleaner
                    onClicked: {
                        if (root.filterModel) {
                            root.filterModel.handleSelection(index, Bridge.currentModifiers())
                        }
                    }
                }
                
                Controls.Label {
                    Layout.fillWidth: true
                    text: model.title + "  —  " + model.author
                    elide: Text.ElideRight
                }
            }
            
            onClicked: {
                if (root.filterModel) {
                    root.filterModel.handleSelection(index, Bridge.currentModifiers())
                }
            }
        }
    }
}
