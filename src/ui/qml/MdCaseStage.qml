// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The MD Visualizer's case area (V2, docs/plans/v2-visualizer-case.md): a case taken out of the
// pile, its cartridge part-way out of the top, flipped to its track list, put back. The one case
// that moves is drawn here; it flies in from the pile (which lies to the left, outside this item:
// nothing clips it, and this item is drawn after the left column). All turns are flat 2D, an
// orthographic box: taking out, the spine face (height · cos θ) folds under the front face
// (height · sin θ); a flip squashes the case to its edge about its vertical centre and opens it on
// the other side. Fixed-size faces are scaled, never resized, so nothing is laid out again while
// it moves. Only `take`, `cartridge` and `flipProgress` animate, one step at a time, and nothing
// runs once the case has settled (Idle, Front, Back). With animations off (screen or window hidden)
// every step completes at once. `takenKey` is the pile's gap; VisualizerView works out where
// the case comes from and goes back to (`takeCase(item, origin)`, `putBack(dest, fade)`).
Item {
    id: root

    enum Phase { Idle, TakingOut, CartridgeOut, Front, FlippingToBack, Back, FlippingToFront, PuttingBack }

    property int phase: MdCaseStage.Idle
    // MdPile's item: { kind, id, title, artist, year, coverUrl, count, color, detail }.
    property var item: null
    property string takenKey: ""
    property bool paneActive: false
    property bool animationsEnabled: true
    property int trackIndex: 0
    property bool showRows: false

    // What animates (0..1), and where the case comes from / goes back to (this item's coordinates).
    property real take: 0
    property real cartridge: 0
    property real flipProgress: 0
    property rect origin: Qt.rect(0, 0, 0, 0)
    property bool fadeOut: false // going back to a pile that isn't showing: fade at the column's edge
    property int putStep: 0
    property string animProperty
    property real animTo: 0

    readonly property bool isOpen: phase !== MdCaseStage.Idle
    readonly property bool settled: phase === MdCaseStage.Front || phase === MdCaseStage.Back
    readonly property bool animating: isOpen && !settled
    readonly property bool flipping: phase === MdCaseStage.FlippingToBack || phase === MdCaseStage.FlippingToFront
    readonly property bool showingBack: flipProgress >= 0.5
    readonly property bool mix: item !== null && item.kind === "playlist"
    readonly property alias trackModel: tracks
    readonly property alias caseItem: theCase
    readonly property alias front: frontFace
    readonly property alias back: backFace
    readonly property alias cartridgeItem: cart
    readonly property alias spineFace: spineFace

    // Layout: the case (taller than wide, like the 68 × 72 mm cartridge it holds), the cartridge's
    // peek above it, and the Flip / Put back buttons beside it (or under it, in a narrow slot).
    readonly property int margin: Theme.spaceMD
    readonly property int buttonsWidth: 132
    readonly property int buttonsRowHeight: 36
    readonly property int sideH: Math.min(Math.floor((height - 2 * margin) / 1.34),
                                          Math.floor((width - 3 * margin - buttonsWidth) / 0.95))
    readonly property int underH: Math.min(Math.floor((height - 3 * margin - buttonsRowHeight) / 1.34),
                                           Math.floor((width - 2 * margin) / 0.95))
    readonly property bool buttonsBeside: sideH >= underH
    readonly property int caseH: Math.max(150, Math.min(340, Math.max(sideH, underH)))
    readonly property int caseW: Math.round(caseH * 0.95)
    readonly property int peek: Math.round(caseH * 0.34)
    readonly property int caseX: buttonsBeside ? Math.round((width - caseW - margin - buttonsWidth) / 2)
                                               : Math.round((width - caseW) / 2)
    readonly property int caseY: buttonsBeside ? Math.round((height - caseH - peek) / 2) + peek
                                               : Math.round((height - caseH - peek - margin - buttonsRowHeight) / 2) + peek
    readonly property rect caseTargetRect: Qt.rect(caseX, caseY, caseW, caseH)
    readonly property int spineRest: Math.round(caseH * 0.13)

    // The orthographic box, taking out (take 0 → 1).
    readonly property real cosT: Math.cos(take * Math.PI / 2)
    readonly property real sinT: Math.sin(take * Math.PI / 2)
    readonly property real curW: origin.width + (caseW - origin.width) * take
    readonly property real spineVisH: (origin.height + (spineRest - origin.height) * take) * cosT
    readonly property real frontVisH: caseH * sinT
    readonly property real totalH: spineVisH + frontVisH
    readonly property real curCx: origin.x + origin.width / 2 + (caseX + caseW / 2 - origin.x - origin.width / 2) * take
    readonly property real curCy: origin.y + origin.height / 2 + (caseY + caseH / 2 - origin.y - origin.height / 2) * take
                                  - Math.sin(take * Math.PI) * 18
    // The flip (flipProgress 0 → 1: front → back).
    readonly property real flipCos: Math.abs(Math.cos(flipProgress * Math.PI))
    readonly property real flipSin: Math.sin(flipProgress * Math.PI)

    signal playTrackRequested(TrackListModel model, int row)
    signal putBackRequested()
    // Put back and gone (VisualizerView then takes out a case chosen meanwhile).
    signal returned()
    // The user clicked the case or its buttons (it takes the keyboard).
    signal interacted()

    function takeCase(it: var, originRect: rect) {
        if (animating)
            finishAnimation();
        item = it;
        takenKey = it.kind + ":" + it.id;
        origin = originRect;
        take = 0;
        cartridge = 0;
        flipProgress = 0;
        fadeOut = false;
        showRows = false;
        trackIndex = 0;
        if (it.kind === "playlist") {
            tracks.albumId = 0;
            tracks.playlistId = it.id;
        } else {
            tracks.playlistId = 0;
            tracks.albumId = it.id;
        }
        phase = MdCaseStage.TakingOut;
        run("take", 1, 480, Easing.OutCubic);
    }

    function putBack(dest: rect, fade: bool) {
        if (!isOpen)
            return;
        if (phase === MdCaseStage.PuttingBack) {
            finishAnimation();
            return;
        }
        if (animating)
            finishAnimation();
        origin = dest;
        fadeOut = fade;
        putStep = 0;
        phase = MdCaseStage.PuttingBack;
        run("cartridge", 0, 150, Easing.InCubic); // tuck the cartridge in, then fold and return
    }

    // Front ↔ back. Pressed again during a flip: the flip finishes (and that's all).
    function flipCase() {
        if (flipping) {
            finishAnimation();
            return;
        }
        if (phase === MdCaseStage.TakingOut || phase === MdCaseStage.CartridgeOut)
            finishAnimation();
        if (phase === MdCaseStage.Front) {
            showRows = true;
            phase = MdCaseStage.FlippingToBack;
            run("flipProgress", 1, 460, Easing.InOutCubic);
        } else if (phase === MdCaseStage.Back) {
            phase = MdCaseStage.FlippingToFront;
            run("flipProgress", 0, 460, Easing.InOutCubic);
        }
    }

    // Enter: on the front it flips; on the back it plays from the cursor's track. During an
    // animation it finishes it.
    function activate() {
        if (animating && phase !== MdCaseStage.PuttingBack) {
            finishAnimation();
            return;
        }
        if (phase === MdCaseStage.Front)
            flipCase();
        else if (phase === MdCaseStage.Back && tracks.count > 0)
            root.playTrackRequested(tracks, trackIndex);
    }

    function moveTrack(delta: int) {
        if (phase === MdCaseStage.FlippingToBack)
            finishAnimation();
        if (phase !== MdCaseStage.Back || tracks.count === 0)
            return;
        trackIndex = Math.max(0, Math.min(tracks.count - 1, trackIndex + delta));
        backFace.positionAt(trackIndex);
    }

    // During an animation: is `action` the one that started it (it then only finishes it)?
    function isRepeat(action: int): bool {
        if (phase === MdCaseStage.TakingOut || phase === MdCaseStage.CartridgeOut)
            return action === KeyRouter.Activate;
        if (flipping)
            return action === KeyRouter.FlipCase || action === KeyRouter.Activate;
        return false;
    }

    // Jumps the running operation (taking out, a flip, putting back) to its end.
    function finishAnimation() {
        const puttingBack = phase === MdCaseStage.PuttingBack;
        for (let guard = 0; animating && guard < 8; ++guard) {
            if (puttingBack && phase !== MdCaseStage.PuttingBack)
                break; // the case chosen meanwhile is on its way out: that one plays
            anim.stop();
            root[animProperty] = animTo;
            stepDone();
        }
    }

    function openContextMenu(target: Item, x: real, y: real) {
        if (!item)
            return;
        const entries = item.kind === "playlist" ? ContextMenus.playlistEntries(item.id, item.title, item.count)
                                                 : ContextMenus.albumEntries(item.id, item.title);
        ContextMenus.open(target, x, y, entries, null);
    }

    // `,` / Menu while the case has the keyboard.
    function openContextMenuForCase() {
        openContextMenu(theCase, Math.round(caseW * 0.35), Math.round(caseH * 0.5));
    }

    function run(prop: string, to: real, ms: int, easing: int) {
        anim.stop();
        animProperty = prop;
        animTo = to;
        if (!animationsEnabled || ms <= 0 || root[prop] === to) {
            root[prop] = to;
            stepDone();
            return;
        }
        anim.property = prop;
        anim.to = to;
        anim.duration = ms;
        anim.easing.type = easing;
        anim.start();
    }

    function stepDone() {
        switch (phase) {
        case MdCaseStage.TakingOut:
            phase = MdCaseStage.CartridgeOut;
            run("cartridge", 1, 420, Easing.OutCubic);
            break;
        case MdCaseStage.CartridgeOut:
            phase = MdCaseStage.Front;
            showRows = true; // the back's rows get built now, between animations
            break;
        case MdCaseStage.FlippingToBack:
            phase = MdCaseStage.Back;
            backFace.positionAt(trackIndex);
            break;
        case MdCaseStage.FlippingToFront:
            phase = MdCaseStage.Front;
            break;
        case MdCaseStage.PuttingBack:
            if (putStep === 0) {
                putStep = 1;
                run("take", 0, 350, Easing.InOutCubic);
            } else {
                finishPutBack();
            }
            break;
        }
    }

    function finishPutBack() {
        phase = MdCaseStage.Idle;
        take = 0;
        cartridge = 0;
        flipProgress = 0;
        fadeOut = false;
        showRows = false;
        trackIndex = 0;
        tracks.playlistId = 0;
        tracks.albumId = 0;
        takenKey = "";
        item = null;
        root.returned();
    }

    onAnimationsEnabledChanged: if (!animationsEnabled) finishAnimation()

    NumberAnimation {
        id: anim
        target: root
        onFinished: root.stepDone()
    }

    TrackListModel {
        id: tracks
        // A reload (tags edited) can shorten the list under the cursor.
        onCountChanged: {
            if (root.trackIndex >= count)
                root.trackIndex = Math.max(0, count - 1);
        }
    }

    MdPlaceholder {
        objectName: "casePlaceholder"
        anchors.fill: parent
        visible: root.phase === MdCaseStage.Idle
        heading: qsTr("Pick a MiniDisc")
        detail: qsTr("Enter or a click takes it out of the pile")
    }

    // ---- the case that moves ----
    Item {
        id: actor
        anchors.fill: parent
        visible: root.isOpen
        opacity: root.fadeOut ? Math.min(1, root.take / 0.4) : 1

        Rectangle { // the shadow under the case
            x: root.curCx - root.curW * root.flipCos / 2 + 6
            y: root.curCy - root.totalH / 2 + 8
            width: root.curW * root.flipCos
            height: root.totalH
            radius: 4
            color: Theme.mdCaseShadow
            opacity: root.take * 0.8
        }

        MdCaseSpine { // folds under the front as the case comes out (and back as it goes)
            id: spineFace
            objectName: "actorSpine"
            x: root.curCx - root.curW / 2
            y: root.curCy - root.totalH / 2 + root.frontVisH
            width: Math.max(1, root.origin.width)
            height: Math.max(1, root.origin.height)
            visible: root.cosT > 0.005
            mix: root.mix
            label: root.item ? root.item.title : ""
            detail: root.item ? root.item.detail : ""
            caseColor: root.item ? root.item.color : Theme.spineDefault
            transform: Scale {
                xScale: root.curW / spineFace.width
                yScale: Math.max(0.001, root.spineVisH / spineFace.height)
            }

            Rectangle { // turning away from the light
                anchors.fill: parent
                color: Theme.mdFaceShade
                opacity: (1 - root.cosT) * 0.45
            }
        }

        Item { // the case itself: cartridge, front, back. Fixed size, scaled to turn and flip.
            id: theCase
            objectName: "caseActor"
            x: root.curCx - root.curW / 2
            y: root.curCy - root.totalH / 2
            width: root.caseW
            height: root.caseH
            visible: root.sinT > 0.005
            transform: [
                Scale {
                    xScale: root.curW / root.caseW
                    yScale: Math.max(0.001, root.frontVisH / root.caseH)
                },
                Scale {
                    origin.x: root.caseW / 2
                    xScale: Math.max(0.001, root.flipCos)
                }
            ]

            Item { // the slit at the top: the cartridge never shows below the case's top part
                id: cartridgeClip
                y: -root.peek
                width: parent.width
                height: root.peek + parent.height
                clip: true

                MdCartridge {
                    id: cart
                    objectName: "cartridge"
                    width: Math.round(root.caseW * 0.92)
                    height: Math.round(root.caseH * 0.92)
                    x: Math.round((parent.width - width) / 2)
                    y: (root.peek + root.caseH * 0.04) * (1 - root.cartridge)
                    visible: root.cartridge > 0.005
                    mix: root.mix
                    caseId: root.item ? root.item.id : 0
                    title: root.item ? root.item.title : ""
                    coverUrl: root.item ? root.item.coverUrl : ""
                    showingBack: root.showingBack
                }
            }

            MdCaseFront {
                id: frontFace
                objectName: "caseFront"
                anchors.fill: parent
                visible: !root.showingBack
                mix: root.mix
                caseId: root.item ? root.item.id : 0
                title: root.item ? root.item.title : ""
                artist: root.item ? root.item.artist : ""
                year: root.item ? root.item.year : 0
                trackCount: root.mix && root.item ? root.item.count : tracks.count
                coverUrl: root.item ? root.item.coverUrl : ""
                caseColor: root.item ? root.item.color : Theme.spineDefault
            }

            MdCaseBack {
                id: backFace
                objectName: "caseBack"
                anchors.fill: parent
                visible: root.showingBack
                mix: root.mix
                caseId: root.item ? root.item.id : 0
                title: root.item ? root.item.title : ""
                caseColor: root.item ? root.item.color : Theme.spineDefault
                trackModel: tracks
                showRows: root.showRows
                currentIndex: root.trackIndex
                active: root.paneActive
                interactive: root.phase === MdCaseStage.Back
                onTrackClicked: row => {
                    root.interacted();
                    root.trackIndex = row;
                }
                onTrackActivated: row => {
                    root.interacted();
                    root.trackIndex = row;
                    root.playTrackRequested(tracks, row);
                }
            }

            Rectangle { // turning away from the light (taking out, flipping)
                anchors.fill: parent
                radius: 3
                color: Theme.mdFaceShade
                opacity: Math.min(0.8, (1 - root.sinT) * 0.45 + (1 - root.flipCos) * 0.38)
                visible: opacity > 0.01
            }
            Rectangle { // the keyboard's case
                anchors.fill: parent
                radius: 3
                color: "transparent"
                border.color: Theme.focusRing
                border.width: 2
                visible: root.paneActive && root.settled
            }

            TapHandler { // the front (or a moving case): a click flips it (or finishes the move)
                acceptedButtons: Qt.LeftButton
                enabled: (!root.showingBack || root.animating) && root.phase !== MdCaseStage.PuttingBack
                onTapped: {
                    root.interacted();
                    if (root.animating && root.phase !== MdCaseStage.PuttingBack)
                        root.finishAnimation();
                    else if (root.phase === MdCaseStage.Front)
                        root.flipCase();
                }
            }
            TapHandler { // not on a case on its way back
                acceptedButtons: Qt.RightButton
                enabled: root.phase !== MdCaseStage.PuttingBack
                onTapped: point => {
                    root.interacted();
                    root.openContextMenu(theCase, point.position.x, point.position.y);
                }
            }
        }

        Rectangle { // the case's thickness at the turning edge, mid-flip
            visible: root.flipping && root.flipSin > 0.02
            x: root.caseX + root.caseW / 2 + root.caseW / 2 * root.flipCos
            y: root.caseY
            width: Math.round(root.spineRest * root.flipSin)
            height: root.caseH
            color: Qt.darker(root.item ? root.item.color : Theme.spineDefault, 1.4)
        }
    }

    GridLayout { // Flip / Put back
        id: buttons
        visible: root.settled || root.flipping // not while the case flies in or out
        x: root.buttonsBeside ? root.caseX + root.caseW + root.margin : root.caseX
        y: root.buttonsBeside ? root.caseY + root.caseH - height : root.caseY + root.caseH + root.margin
        width: root.buttonsBeside ? root.buttonsWidth : root.caseW
        columns: root.buttonsBeside ? 1 : 2
        rowSpacing: Theme.spaceXS
        columnSpacing: Theme.spaceXS

        Button {
            objectName: "flipButton"
            Layout.fillWidth: true
            text: root.showingBack ? qsTr("Front  (f)") : qsTr("Flip  (f)")
            focusPolicy: Qt.NoFocus
            onClicked: {
                root.interacted();
                root.flipCase();
            }
        }
        Button {
            objectName: "putBackButton"
            Layout.fillWidth: true
            text: qsTr("Put back  (Esc)")
            focusPolicy: Qt.NoFocus
            onClicked: {
                root.interacted();
                root.putBackRequested();
            }
        }
    }
}
