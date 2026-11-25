import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.endpaper 1.0

ColumnLayout {
    id: root

    property var filterModel
    property string searchPlaceholder: "Filter..."
    property string emptyText: "No items loaded"

    Kirigami.SearchField {
        Layout.fillWidth: true
        placeholderText: root.searchPlaceholder
        text: root.filterModel ? root.filterModel.filterString : ""
        onTextChanged: {
            if (root.filterModel && root.filterModel.filterString !== text) {
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

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - (Kirigami.Units.largeSpacing * 4)
            visible: listView.count === 0
            text: root.emptyText
        }

        delegate: Controls.ItemDelegate {
            id: delegateItem
            width: ListView.view.width

            topPadding: Kirigami.Units.smallSpacing
            bottomPadding: Kirigami.Units.smallSpacing

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Controls.CheckBox {
                    checked: model.selected
                    onToggled: {
                        checked = Qt.binding(function() { return model.selected })
                        if (root.filterModel) {
                            root.filterModel.handleSelection(index, Bridge.currentModifiers())
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Controls.Label {
                        Layout.fillWidth: true
                        text: model.title
                        elide: Text.ElideRight
                        font.bold: model.selected
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        readonly property string dateStr: model.date || ""
                        readonly property string authorStr: model.author || ""
                        text: (authorStr !== "" && dateStr !== "")
                              ? (authorStr + "  •  " + dateStr)
                              : (authorStr !== "" ? authorStr : dateStr)
                        visible: text !== ""
                        elide: Text.ElideRight
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
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
