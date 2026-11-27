// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The MD Visualizer's right side (V3, docs/plans/v3-visualizer-playerItem.md): the case area
// (MdCaseStage) above the player (MdPlayer), side by side in a short window, and the cartridge
// that travels between them. Insert: the cartridge slides out of the case, flies to the player while
// its lid opens, drops in (playback starts as it lands) and the lid shuts. Eject: playback stops,
// the lid opens, the cartridge lifts out and flies back into its case (the lid shutting behind it),
// and VisualizerView puts the case back in the pile. One NumberAnimation steps through the phases
// (as in MdCaseStage); with animations off every step completes at once; finishAnimation()
// completes the running sequence. The player mirrors Playback (the user's rule: the visualizer
// shows what the library is playing): a disc stays in while the playing track is one of its own;
// anything else playing is shown as that album's disc, at once.
Item {
    id: root

    enum Phase { Empty, PullingOut, FlyingIn, DroppingIn, ClosingLid, Loaded, OpeningLid, LiftingOut, FlyingBack, ReturningCase }

    property bool animationsEnabled: true
    property bool casePaneActive: false
    property int phase: MdDeck.Empty
    // The disc in the player: { source: "visualizer" | "external", kind, id, title, artist, coverUrl, color }.
    property var disc: null

    // The flight: from / to (this item's coordinates), progress, a turn on the way (1: back → front,
    // 2: front → back), fading out (an external disc has no case to go back to).
    property rect flyFrom: Qt.rect(0, 0, 0, 0)
    property rect flyTo: Qt.rect(0, 0, 0, 0)
    property real fly: 0
    property int flipMode: 0
    property bool fadingOut: false
    property real lid: 0
    property var playModel: null
    property int playRow: 0
    // Playback asked to start (insert): don't treat what's playing until then as "elsewhere".
    property bool awaitingPlay: false
    property bool playOnLanding: true // false: an Eject cut the insert short
    property var animTarget: null
    property string animProperty
    property real animTo: 0

    readonly property bool hasDisc: phase !== MdDeck.Empty
    readonly property bool loaded: phase === MdDeck.Loaded
    readonly property bool animating: hasDisc && !loaded
    readonly property bool inserting: phase === MdDeck.PullingOut || phase === MdDeck.FlyingIn
                                      || phase === MdDeck.DroppingIn || phase === MdDeck.ClosingLid
    readonly property bool ejecting: phase === MdDeck.OpeningLid || phase === MdDeck.LiftingOut
                                     || phase === MdDeck.FlyingBack || phase === MdDeck.ReturningCase
    readonly property bool flying: phase === MdDeck.FlyingIn || phase === MdDeck.DroppingIn
                                   || phase === MdDeck.LiftingOut || phase === MdDeck.FlyingBack
    readonly property bool stacked: height >= 500 || width < 520
    readonly property int gap: Theme.spaceMD
    readonly property alias caseStage: stageItem
    readonly property alias player: playerItem
    readonly property alias flyingCartridge: flyCart
    readonly property bool hinting: hintTimer.running

    // The cartridge is back in its case: put the case back in the pile.
    signal returnCaseRequested()
    // Playback started elsewhere: the player shows that disc now; a case that's out goes back.
    signal externalLoaded()
    signal interacted()

    // The case that's out into the player, playing from `row` of `model` (its track list).
    function insert(model: var, row: int): bool {
        if (phase !== MdDeck.Empty || !stageItem.settled || stageItem.cartridgeRemoved || !stageItem.item)
            return false;
        const it = stageItem.item;
        disc = { source: "visualizer", kind: it.kind, id: it.id, title: it.title, artist: it.artist,
                 coverUrl: it.coverUrl, color: it.color };
        playModel = model;
        playRow = row;
        flipMode = stageItem.showingBack ? 1 : 0;
        fadingOut = false;
        playOnLanding = true;
        phase = MdDeck.PullingOut;
        run(stageItem, "cartridge", stageItem.pullOut, 300, Easing.InOutCubic);
        return true;
    }

    // Stop, and the disc back to where it came from. Pressed again: done at once.
    function eject(): bool {
        if (phase === MdDeck.Empty)
            return false;
        if (ejecting) {
            finishAnimation();
            return true;
        }
        if (inserting) {
            playOnLanding = false; // landing at once mustn't start what's about to stop
            finishAnimation();
        }
        if (stageItem.animating)
            stageItem.finishAnimation(); // the empty case mid-flip: measure it settled
        // The phase first: stop() may report back at once, and a Loaded deck would take that as
        // playback elsewhere.
        phase = MdDeck.OpeningLid;
        awaitingPlay = false;
        playTimeout.stop();
        Playback.stop();
        run(root, "lid", 1, 300, Easing.OutCubic);
        return true;
    }

    // During a sequence: is `action` the one that started it (it then only finishes it)?
    function isRepeat(action: int): bool {
        if (inserting)
            return action === KeyRouter.PlayCase || action === KeyRouter.Activate;
        if (ejecting)
            return action === KeyRouter.Eject;
        return false;
    }

    // Jumps the running sequence (insert or eject) to its end.
    function finishAnimation() {
        for (let guard = 0; animating && guard < 16; ++guard) {
            anim.stop();
            animTarget[animProperty] = animTo;
            stepDone();
        }
    }

    // The jog's middle: play / pause; with the queue emptied meanwhile, the disc plays again.
    function playPause() {
        if (Playback.hasTrack || !loaded || !disc) {
            Playback.playPause();
        } else if (disc.source === "visualizer" && stageItem.cartridgeRemoved) {
            Playback.playFromModel(stageItem.trackModel, 0);
        } else if (disc.id > 0) {
            Playback.playItem(disc.kind, disc.id);
        }
    }

    function showHint() {
        hintTimer.restart();
    }

    function run(target: var, prop: string, to: real, ms: int, easing: int) {
        anim.stop();
        animTarget = target;
        animProperty = prop;
        animTo = to;
        if (!animationsEnabled || ms <= 0 || target[prop] === to) {
            target[prop] = to;
            stepDone();
            return;
        }
        anim.target = target;
        anim.property = prop;
        anim.to = to;
        anim.duration = ms;
        anim.easing.type = easing;
        anim.start();
    }

    function stepDone() {
        switch (phase) {
        case MdDeck.PullingOut: // the cartridge is clear of the case: the deck's own takes over
            flyFrom = stageItem.cartridgeRect(root);
            flyTo = playerItem.aboveBayRect(root);
            stageItem.cartridgeRemoved = true;
            fly = 0;
            phase = MdDeck.FlyingIn;
            run(root, "fly", 1, 450, Easing.InOutCubic); // the lid opens along (onFlyChanged)
            break;
        case MdDeck.FlyingIn:
            lid = 1;
            flyFrom = flyTo;
            flyTo = playerItem.cartridgeRect(root);
            flipMode = 0;
            fly = 0;
            phase = MdDeck.DroppingIn;
            run(root, "fly", 1, 300, Easing.InCubic);
            break;
        case MdDeck.DroppingIn: // seated: the music starts as the lid shuts
            phase = MdDeck.ClosingLid;
            startPlayback();
            run(root, "lid", 0, 250, Easing.OutCubic);
            break;
        case MdDeck.ClosingLid:
            phase = MdDeck.Loaded;
            followPlayback(); // what Playback said while the lid shut was ignored
            break;
        case MdDeck.OpeningLid:
            flyFrom = playerItem.cartridgeRect(root);
            flyTo = playerItem.aboveBayRect(root);
            flipMode = 0;
            fly = 0;
            phase = MdDeck.LiftingOut;
            run(root, "fly", 1, 250, Easing.OutCubic);
            break;
        case MdDeck.LiftingOut:
            flyFrom = flyTo;
            if (disc && disc.source === "visualizer" && stageItem.cartridgeRemoved) {
                stageItem.cartridge = stageItem.pullOut; // the window may have changed size since
                flyTo = stageItem.cartridgeRect(root); // where it left the case
                flipMode = stageItem.showingBack ? 2 : 0;
            } else { // an external disc: up and away
                flyTo = Qt.rect(flyFrom.x, flyFrom.y - Math.round(playerItem.h * 0.4), flyFrom.width, flyFrom.height);
                fadingOut = true;
            }
            fly = 0;
            phase = MdDeck.FlyingBack;
            run(root, "fly", 1, 450, Easing.InOutCubic); // the lid shuts along
            break;
        case MdDeck.FlyingBack:
            lid = 0;
            if (fadingOut) {
                finishEject(false);
                break;
            }
            stageItem.cartridgeRemoved = false;
            phase = MdDeck.ReturningCase;
            run(stageItem, "cartridge", 0, 350, Easing.InOutCubic);
            break;
        case MdDeck.ReturningCase:
            finishEject(true);
            break;
        }
    }

    function startPlayback() {
        if (!playOnLanding)
            return;
        awaitingPlay = true;
        playTimeout.restart();
        if (playModel && playModel.count > playRow)
            Playback.playFromModel(playModel, playRow);
        else if (disc)
            Playback.playItem(disc.kind, disc.id); // the track list hadn't loaded: by id, from the top
    }

    function finishEject(caseBack: bool) {
        phase = MdDeck.Empty;
        disc = null;
        fly = 0;
        lid = 0;
        flipMode = 0;
        fadingOut = false;
        playModel = null;
        if (caseBack)
            root.returnCaseRequested();
        followPlayback(); // something started elsewhere during the eject: its disc goes in now
    }

    // Is the playing track the loaded disc's? A visualizer disc: one of its case's tracks (a mix
    // disc's come from many albums); an external disc: the same album.
    function isOurs(trackId: int): bool {
        if (!disc)
            return false;
        if (disc.source === "external") // tracks without an album: the same track
            return disc.id > 0 ? Playback.currentAlbumId === disc.id : trackId === disc.trackId;
        const m = stageItem.trackModel;
        if (!stageItem.cartridgeRemoved)
            return true; // its cartridge is on its way back to the case
        // (A reload keeps the old rows until the new ones arrive, so an empty list is empty.)
        for (let i = 0; i < m.count; ++i) {
            if (m.get(i).trackId === trackId)
                return true;
        }
        return false;
    }

    // Playback changed: keep the disc, or show what's playing instead (mid-sequence: ignored).
    function followPlayback() {
        if (animating)
            return;
        if (awaitingPlay) {
            if (Playback.playing && isOurs(Playback.currentTrackId)) {
                awaitingPlay = false;
                playTimeout.stop();
            }
            return;
        }
        if (!Playback.hasTrack || Playback.currentTrackId <= 0)
            return; // stopped and cleared: the disc stays in
        if (loaded && isOurs(Playback.currentTrackId))
            return;
        if (!loaded && !Playback.playing)
            return; // a paused or stopped queue doesn't put a disc in
        disc = { source: "external", kind: "album", id: Playback.currentAlbumId, trackId: Playback.currentTrackId,
                 title: Playback.album !== "" ? Playback.album : Playback.title, artist: Playback.artist,
                 coverUrl: Playback.coverUrl, color: Theme.spineDefault };
        anim.stop();
        fly = 0;
        lid = 0;
        flipMode = 0;
        fadingOut = false;
        playModel = null;
        phase = MdDeck.Loaded;
        root.externalLoaded();
    }

    onFlyChanged: {
        if (phase === MdDeck.FlyingIn)
            lid = Math.min(1, fly * 1.4);
        else if (phase === MdDeck.FlyingBack)
            lid = Math.max(0, 1 - fly * 1.4);
    }
    onAnimationsEnabledChanged: if (!animationsEnabled) finishAnimation()
    onStackedChanged: if (animating) finishAnimation() // its rects were for the old layout
    Component.onCompleted: followPlayback()

    Connections {
        target: Playback
        function onCurrentChanged() {
            root.followPlayback();
        }
        function onStateChanged() {
            root.followPlayback();
        }
    }

    NumberAnimation {
        id: anim
        onFinished: root.stepDone()
    }
    Timer { // a play that never started (a missing file): stop waiting for it
        id: playTimeout
        interval: 5000
        onTriggered: {
            root.awaitingPlay = false;
            root.followPlayback();
        }
    }
    Timer {
        id: hintTimer
        interval: 2000
    }

    MdCaseStage {
        id: stageItem
        objectName: "caseStage"
        z: 1 // the case flies over the player's cell
        width: root.stacked ? root.width : Math.round((root.width - root.gap) / 2)
        height: root.stacked ? Math.round((root.height - root.gap) * 0.52) : root.height
        paneActive: root.casePaneActive
        animationsEnabled: root.animationsEnabled
        controlsShown: !root.animating
    }

    MdPlayer {
        id: playerItem
        objectName: "player"
        x: root.stacked ? 0 : stageItem.width + root.gap
        y: root.stacked ? stageItem.height + root.gap : 0
        width: root.stacked ? root.width : root.width - stageItem.width - root.gap
        height: root.stacked ? root.height - stageItem.height - root.gap : root.height
        // At the case's scale (its cartridge the same size in both), as far as the cell allows.
        bodyH: Math.max(60, Math.min(Math.round(stageItem.caseH * 1.12),
                                     Math.floor((height - 2 * Theme.spaceSM) / 1.1),
                                     Math.floor((width - 2 * Theme.spaceSM) / 1.19)))
        disc: root.disc
        discVisible: root.loaded || root.phase === MdDeck.ClosingLid || root.phase === MdDeck.OpeningLid
        lidOpen: root.lid
        animationsEnabled: root.animationsEnabled
        discTurning: root.loaded && Playback.playing && root.isOurs(Playback.currentTrackId)
        discPaused: root.loaded && Playback.paused
        onEjectRequested: root.eject()
        onPlayPauseRequested: root.playPause()
        onInteracted: root.interacted()
    }

    Label { // taking a case out while a disc is in
        objectName: "ejectHint"
        visible: hintTimer.running
        anchors.horizontalCenter: playerItem.horizontalCenter
        y: playerItem.y + playerItem.height - height
        text: qsTr("Eject the disc first (O)")
        color: Theme.textSecondary
        padding: Theme.spaceXS
        background: Rectangle {
            color: Theme.surfaceElevated
            radius: Theme.radiusSmall
        }
    }

    // ---- the cartridge on its way between the case and the player ----
    // Drawn behind the case (z 1) and in front of the player (z 0): it leaves the case through the
    // top and travels behind it; in the stacked layout it swings out to the right on the way.
    MdCartridge {
        id: flyCart
        objectName: "flyingCartridge"
        z: 0.5
        visible: root.flying
        width: Math.max(1, playerItem.cartW)
        height: Math.max(1, playerItem.cartH)
        readonly property bool travelling: (root.phase === MdDeck.FlyingIn || root.phase === MdDeck.FlyingBack) && !root.fadingOut
        readonly property real fx: root.flyFrom.x + (root.flyTo.x - root.flyFrom.x) * root.fly
                                   + (root.stacked && travelling ? Math.sin(root.fly * Math.PI) * stageItem.caseW * 0.8 : 0)
        readonly property real fy: root.flyFrom.y + (root.flyTo.y - root.flyFrom.y) * root.fly
                                   - (travelling ? Math.sin(root.fly * Math.PI) * 20 : 0)
        readonly property real fw: root.flyFrom.width + (root.flyTo.width - root.flyFrom.width) * root.fly
        readonly property real fh: root.flyFrom.height + (root.flyTo.height - root.flyFrom.height) * root.fly
        readonly property real turn: root.flipMode === 0 ? 1 : Math.max(0.001, Math.abs(Math.cos(root.fly * Math.PI)))
        x: fx + fw / 2 - width / 2
        y: fy
        opacity: root.fadingOut ? 1 - root.fly : 1
        transform: Scale {
            origin.x: flyCart.width / 2
            xScale: flyCart.fw / flyCart.width * flyCart.turn
            yScale: flyCart.fh / flyCart.height
        }
        mix: root.disc !== null && root.disc.kind === "playlist"
        caseId: root.disc ? root.disc.id : 0
        title: root.disc ? root.disc.title : ""
        coverUrl: root.disc ? root.disc.coverUrl : ""
        showingBack: root.flipMode === 1 ? root.fly < 0.5 : root.flipMode === 2 ? root.fly >= 0.5 : false
    }
}
