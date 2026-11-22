// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Artwork finder (M7): albums on the left, cover candidates for the current one on the
// right (MusicBrainz + Cover Art Archive, via ArtFinder). openMissing() reviews every album
// without art, openAlbum() finds (or replaces) one album's cover.
//
// Keyboard (routed by KeyRouter: the content has `routeKeys`; Main calls handleAction()):
// j/k move between albums or rows of covers, h/l between the album list and the covers,
// Enter uses the selected cover (then moves to the next album), s skips the album, r
// searches again, Esc closes. The best match is selected, so Enter, Enter, … works through
// a batch.
Dialog {
    id: root

    readonly property alias artFinder: finder
    property bool batch: true
    property int focusPane: 0 // 0 = albums, 1 = covers
    property int candidateIndex: 0
    readonly property int columns: 3
    readonly property var current: finder.current
    readonly property int currentStatus: current.status ?? ArtFinder.Pending
    readonly property int shownAlbumId: current.albumId ?? 0

    function openMissing() {
        batch = true;
        focusPane = 0;
        finder.openMissing();
        open();
    }

    function openAlbum(albumId: int) {
        batch = false;
        focusPane = 1; // no album list: the covers are all there is
        finder.openAlbum(albumId);
        open();
    }

    function moveAlbum(delta: int) {
        if (finder.count > 0)
            finder.currentIndex = Math.max(0, Math.min(finder.count - 1, finder.currentIndex + delta));
    }

    function moveCandidate(delta: int) {
        const n = finder.candidates.length;
        if (n > 0)
            candidateIndex = Math.max(0, Math.min(n - 1, candidateIndex + delta));
    }

    // To the next album that still needs a decision, if any.
    function advance() {
        const next = finder.nextOpenRow(finder.currentIndex + 1);
        if (next >= 0)
            finder.currentIndex = next;
    }

    function acceptCurrent() {
        if (finder.accept(candidateIndex) && batch)
            advance();
    }

    function skipCurrent() {
        finder.skip();
        if (batch)
            advance();
        else
            close();
    }

    function handleAction(action: int): bool {
        switch (action) {
        case KeyRouter.MoveDown:
        case KeyRouter.MoveUp: {
            const delta = action === KeyRouter.MoveDown ? 1 : -1;
            if (focusPane === 0)
                moveAlbum(delta);
            else
                moveCandidate(delta * columns);
            return true;
        }
        case KeyRouter.MoveLeft:
            if (focusPane === 1 && candidateIndex % columns !== 0)
                --candidateIndex;
            else if (batch)
                focusPane = 0;
            return true;
        case KeyRouter.MoveRight:
            if (focusPane === 0) {
                if (finder.candidates.length > 0)
                    focusPane = 1;
            } else if (candidateIndex + 1 < finder.candidates.length && (candidateIndex + 1) % columns !== 0) {
                ++candidateIndex;
            }
            return true;
        case KeyRouter.Activate:
            acceptCurrent();
            return true;
        case KeyRouter.Skip:
            skipCurrent();
            return true;
        case KeyRouter.Retry:
            finder.retry();
            return true;
        case KeyRouter.Back:
            close();
            return true;
        }
        return false;
    }

    modal: true
    focus: true
    // Esc comes through the KeyRouter (Back); a stray click outside mustn't close it.
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Overlay.overlay ? Math.min(1100, Overlay.overlay.width - 2 * Theme.spaceXL) : 1100
    height: Overlay.overlay ? Math.min(720, Overlay.overlay.height - 2 * Theme.spaceXL) : 720
    padding: Theme.spaceLG
    title: batch ? qsTr("Find missing cover art") : qsTr("Find cover art")

    onOpened: content.forceActiveFocus()
    onClosed: finder.close()
    onShownAlbumIdChanged: candidateIndex = 0
    onCurrentStatusChanged: {
        if (batch && focusPane === 1 && finder.candidates.length === 0)
            focusPane = 0;
    }

    ArtFinder {
        id: finder
        onSaved: (albumId, ok) => {
            if (ok && !root.batch)
                root.close();
        }
    }

    header: RowLayout {
        spacing: Theme.spaceMD

        Label {
            Layout.leftMargin: Theme.spaceLG
            Layout.topMargin: Theme.spaceLG
            text: root.title
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }
        BusyIndicator {
            Layout.topMargin: Theme.spaceLG
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            running: finder.loading || (finder.count > 0 && finder.searchedCount < finder.count)
            visible: running
        }
        Label {
            objectName: "artProgress"
            Layout.topMargin: Theme.spaceLG
            Layout.fillWidth: true
            visible: root.batch && finder.count > 0
            text: qsTr("Searched %1 of %2 · saved %3 · skipped %4")
                .arg(finder.searchedCount).arg(finder.count).arg(finder.savedCount).arg(finder.skippedCount)
            color: Theme.textMuted
            elide: Text.ElideRight
        }
        Item {
            Layout.fillWidth: true
            visible: !root.batch || finder.count === 0
        }
        CheckBox {
            objectName: "showSkippedBox"
            Layout.topMargin: Theme.spaceLG
            visible: root.batch
            text: Library.skippedArtCount > 0 || finder.includeSkipped
                  ? qsTr("Show skipped (%1)").arg(Library.skippedArtCount) : qsTr("Show skipped")
            focusPolicy: Qt.NoFocus
            checked: finder.includeSkipped
            onToggled: {
                finder.includeSkipped = checked;
                finder.openMissing();
            }
        }
        ToolButton {
            Layout.rightMargin: Theme.spaceSM
            Layout.topMargin: Theme.spaceSM
            text: "✕"
            focusPolicy: Qt.NoFocus
            onClicked: root.close()
        }
    }

    footer: RowLayout {
        spacing: Theme.spaceSM

        Label {
            Layout.leftMargin: Theme.spaceLG
            Layout.bottomMargin: Theme.spaceLG
            Layout.fillWidth: true
            text: qsTr("j/k albums · h/l covers · Enter use cover · s skip · r search again · Esc close")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
        Button {
            Layout.bottomMargin: Theme.spaceLG
            text: qsTr("Skip")
            focusPolicy: Qt.NoFocus
            enabled: root.shownAlbumId !== 0 && root.currentStatus !== ArtFinder.Saving
                     && root.currentStatus !== ArtFinder.Saved
            onClicked: root.skipCurrent()
        }
        Button {
            Layout.bottomMargin: Theme.spaceLG
            text: qsTr("Search again")
            focusPolicy: Qt.NoFocus
            enabled: root.shownAlbumId !== 0 && root.currentStatus !== ArtFinder.Saving
            onClicked: finder.retry()
        }
        Button {
            objectName: "useCoverButton"
            Layout.bottomMargin: Theme.spaceLG
            Layout.rightMargin: Theme.spaceLG
            text: root.currentStatus === ArtFinder.Saving ? qsTr("Saving…") : qsTr("Use this cover")
            highlighted: true
            focusPolicy: Qt.NoFocus
            enabled: finder.candidates.length > 0 && root.currentStatus !== ArtFinder.Saving
            onClicked: root.acceptCurrent()
        }
    }

    // Keyboard focus lives here: `routeKeys` lets the KeyRouter handle keys inside the popup.
    Item {
        id: content

        property bool routeKeys: true

        anchors.fill: parent
        focus: true

        Label {
            objectName: "artEmptyLabel"
            anchors.centerIn: parent
            visible: !finder.loading && finder.count === 0
            text: root.batch ? (finder.includeSkipped ? qsTr("Every album has cover art.")
                                                      : qsTr("Every album has cover art (or was skipped)."))
                             : qsTr("This album isn't in the library any more.")
            color: Theme.textMuted
        }

        RowLayout {
            anchors.fill: parent
            visible: finder.count > 0
            spacing: Theme.spaceLG

            // --- Albums ---
            Rectangle {
                Layout.preferredWidth: Math.round(parent.width * 0.34)
                Layout.fillHeight: true
                visible: root.batch
                color: Theme.surface
                radius: Theme.radiusSmall
                border.color: Theme.border

                FocusBar {
                    active: root.focusPane === 0
                }

                ListView {
                    id: albumList
                    objectName: "artAlbumList"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: finder
                    currentIndex: finder.currentIndex
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: NavRow {
                        id: albumRow

                        required property int index
                        required property string title
                        required property string artist
                        required property int year
                        required property int status
                        required property int candidateCount
                        required property string coverUrl

                        width: ListView.view.width
                        current: index === finder.currentIndex
                        paneActive: root.focusPane === 0
                        onClicked: {
                            finder.currentIndex = index;
                            root.focusPane = 0;
                        }

                        contentItem: RowLayout {
                            spacing: Theme.spaceMD

                            Label {
                                Layout.preferredWidth: 22
                                horizontalAlignment: Text.AlignHCenter
                                text: ["·", "…", String(albumRow.candidateCount), "–", "!", "↻", "✓", "⤼"][albumRow.status]
                                color: albumRow.status === ArtFinder.Saved ? Theme.accent
                                     : albumRow.status === ArtFinder.Failed ? Theme.errorText
                                     : albumRow.status === ArtFinder.Found ? Theme.textPrimary : Theme.textMuted
                                font.bold: albumRow.status === ArtFinder.Found || albumRow.status === ArtFinder.Saved
                            }
                            CoverArt {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                source: albumRow.coverUrl
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: albumRow.title
                                    elide: Text.ElideRight
                                    color: albumRow.status === ArtFinder.Skipped ? Theme.textMuted : Theme.textPrimary
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: albumRow.year > 0 ? albumRow.artist + " · " + albumRow.year : albumRow.artist
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            // --- The current album and its covers ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spaceMD

                Label {
                    objectName: "artAlbumTitle"
                    Layout.fillWidth: true
                    text: root.current.title ?? ""
                    font.pixelSize: Theme.fontHeading
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        const c = root.current;
                        const parts = [c.artist ?? ""];
                        if ((c.year ?? 0) > 0)
                            parts.push(c.year);
                        if ((c.trackCount ?? 0) > 0)
                            parts.push(c.trackCount === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(c.trackCount));
                        return parts.join(" · ");
                    }
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.current.folder ?? ""
                    visible: text !== ""
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    elide: Text.ElideMiddle
                }
                Label {
                    objectName: "artError"
                    Layout.fillWidth: true
                    text: root.current.error ?? ""
                    visible: text !== ""
                    color: Theme.errorText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    FocusBar {
                        active: root.focusPane === 1
                    }

                    // Searching / nothing found / saved: a message in the middle.
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, 420)
                        visible: finder.candidates.length === 0 || root.currentStatus === ArtFinder.Saved
                        spacing: Theme.spaceMD

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: root.currentStatus === ArtFinder.Pending || root.currentStatus === ArtFinder.Searching
                                     || root.currentStatus === ArtFinder.Saving
                            visible: running
                        }
                        CoverArt {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 200
                            visible: root.currentStatus === ArtFinder.Saved
                            source: root.current.coverUrl ?? ""
                        }
                        Label {
                            objectName: "artStatusLabel"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            text: {
                                switch (root.currentStatus) {
                                case ArtFinder.Pending: return qsTr("Waiting for its turn on MusicBrainz…");
                                case ArtFinder.Searching: return qsTr("Searching MusicBrainz and the Cover Art Archive…");
                                case ArtFinder.NotFound: return qsTr("No cover found. Press r to search again or s to skip.");
                                case ArtFinder.Failed: return qsTr("The search failed. Press r to try again.");
                                case ArtFinder.Saving: return qsTr("Saving the cover…");
                                case ArtFinder.Saved: return qsTr("Saved: cover file in the album folder, embedded in the tracks.");
                                case ArtFinder.Skipped: return qsTr("Skipped. It won't be listed again unless you show skipped albums.");
                                }
                                return "";
                            }
                        }
                    }

                    GridView {
                        id: candidateGrid
                        objectName: "artCandidateGrid"
                        anchors.fill: parent
                        anchors.topMargin: Theme.spaceSM
                        visible: finder.candidates.length > 0 && root.currentStatus !== ArtFinder.Saved
                        clip: true
                        model: finder.candidates
                        cellWidth: Math.floor(width / root.columns)
                        cellHeight: cellWidth + 64
                        currentIndex: root.candidateIndex
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            id: card

                            required property int index
                            required property var modelData
                            readonly property bool selected: index === root.candidateIndex

                            width: candidateGrid.cellWidth
                            height: candidateGrid.cellHeight

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: Theme.spaceSM
                                color: card.selected ? Theme.rowSelected : "transparent"
                                radius: Theme.radiusMedium
                                border.width: card.selected ? 2 : 1
                                border.color: card.selected ? (root.focusPane === 1 ? Theme.focusRing : Theme.accentMuted)
                                                            : Theme.borderSubtle

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spaceSM
                                    spacing: 2

                                    CoverArt {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: width
                                        source: card.modelData.previewUrl
                                        requestSize: 500

                                        Label {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.margins: Theme.spaceXS
                                            visible: card.index === 0
                                            text: qsTr("Best match")
                                            padding: 3
                                            font.pixelSize: Theme.fontMicro
                                            color: Theme.onAccent
                                            background: Rectangle {
                                                color: Theme.accent
                                                radius: Theme.radiusSmall
                                            }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: card.modelData.title
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            const d = card.modelData;
                                            const parts = [];
                                            if (d.year > 0)
                                                parts.push(d.year);
                                            if (d.country !== "")
                                                parts.push(d.country);
                                            if (d.format !== "")
                                                parts.push(d.format);
                                            if (d.trackCount > 0)
                                                parts.push(qsTr("%1 tracks").arg(d.trackCount));
                                            if (d.fromReleaseGroup)
                                                parts.push(qsTr("any edition"));
                                            return parts.join(" · ");
                                        }
                                        font.pixelSize: Theme.fontSmall
                                        color: Theme.textMuted
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.candidateIndex = card.index;
                                    root.focusPane = 1;
                                }
                                onDoubleClicked: {
                                    root.candidateIndex = card.index;
                                    root.acceptCurrent();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
