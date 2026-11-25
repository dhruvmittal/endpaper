import QtQuick
import QtQuick.Controls as Controls
import org.kde.endpaper 1.0

Controls.ComboBox {
    id: control
    model: Bridge.bookModel
    textRole: "displayTitle"
    valueRole: "id"

    function syncIndex() {
        const idx = Bridge.bookModel.indexOf(Bridge.selectedBookId)
        if (currentIndex !== idx) {
            currentIndex = idx
        }
    }

    Component.onCompleted: syncIndex()
    onActivated: if (currentValue !== undefined) Bridge.selectedBookId = currentValue

    Connections {
        target: Bridge
        function onSelectedBookIdChanged() { control.syncIndex() }
    }
    Connections {
        target: Bridge.bookModel
        function onModelReset() { control.syncIndex() }
        function onCountChanged() { control.syncIndex() }
    }
}
