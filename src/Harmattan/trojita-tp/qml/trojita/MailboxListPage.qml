import QtQuick 1.1
import com.nokia.meego 1.0
import com.nokia.extras 1.1

Page {
    signal mailboxSelected(string mailbox)
    property int nestingDepth: 0

    function setMailboxModel(model) {
        proxyModel.model = model
        view.model = proxyModel
    }

    function openParentMailbox() {
        view.model.rootIndex = view.model.parentModelIndex()
        --nestingDepth
    }

    function isNestedSomewhere() {
        return nestingDepth > 0
    }

    anchors.margins: UiConstants.DefaultMargin
    tools: commonTools

    //Component.onCompleted: {theme.inverted = true}

    Component {
        id: mailboxItemDelegate

        Item {
            width: parent.width
            height: UiConstants.ListItemHeightDefault

            Item {
                anchors {
                    top: parent.top; bottom: parent.bottom; left: parent.left;
                    right: moreIndicator.visible ? moreIndicator.left : parent.right
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        view.positionViewAtIndex(model.index, ListView.Visible);
                        view.currentIndex = model.index
                        if (mailboxIsSelectable) {
                            mailboxSelected(mailboxName)
                        }
                    }
                }
                Text {
                    id: titleText
                    text: shortMailboxName
                    font: UiConstants.TitleFont
                }
                ProgressBar {
                    visible: mailboxIsSelectable && totalMessageCount === undefined
                    anchors { left: parent.left; right: parent.right; top: titleText.bottom }
                    indeterminate: true
                }
                Text {
                    id: messageCountsText
                    anchors.top: titleText.bottom
                    font: UiConstants.SubtitleFont
                    visible: mailboxIsSelectable && totalMessageCount !== undefined
                    text: totalMessageCount === 0 ?
                              "empty" :
                              (totalMessageCount + " total, " + unreadMessageCount + " unread")
                }
            }

            MoreIndicator {
                id: moreIndicator
                visible: mailboxHasChildMailboxes
                anchors {verticalCenter: parent.verticalCenter; right: parent.right}

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        view.positionViewAtIndex(model.index, ListView.Visible);
                        view.currentIndex = model.index
                        view.model.rootIndex = view.model.modelIndex(index)
                        ++nestingDepth
                    }
                }
            }
        }
    }

    VisualDataModel {
        id: proxyModel
        delegate: mailboxItemDelegate
    }

    ListView {
        id: view
        anchors.fill: parent
        highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
        highlightMoveDuration: 600
        focus: true
    }

    ScrollDecorator {
        flickableItem: view
    }
}