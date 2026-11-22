// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// The tag editor in the app shell: marking tracks on the album page, opening the dialog
// with e / E, and saving through it. Kid A/01.flac is the fixture's one real file (02 is
// missing on disk); its title is put back after editing.
TestCase {
    id: tc
    name: "TagEditor"
    when: windowShown

    Component {
        id: mainComp
        Main {}
    }

    function cleanup() {
        Playback.stop(); // clearing keeps a playing track
        Playback.clearQueue();
        Library.clearError();
    }

    function act(win, action, times) {
        for (let i = 0; i < (times === undefined ? 1 : times); ++i)
            win.handleAction(action);
    }

    // Opens Kid A from the browse pane's album list, keyboard focus on its tracks.
    function openKidA(win) {
        const lib = win.libraryView;
        tryCompare(Library, "trackCount", 12);
        win.showScreen(Main.LibraryScreen);
        lib.browse.setMode(BrowsePane.Albums);
        tryVerify(() => lib.browse.currentList.count === 5);
        const model = lib.browse.currentList.model;
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).title === "Kid A")
                lib.applySelection(BrowsePane.Albums, model.get(i));
        }
        lib.focusContent();
        compare(lib.content, LibraryView.Album);
        tryCompare(lib.albumPage, "trackCount", 2);
        return lib;
    }

    // MMM_SCREENSHOT_DIR: the whole window, popups included.
    function shot(win, name) {
        if (screenshotDir === "")
            return;
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/" + name + ".png");
    }

    function waitLoaded(dialog) {
        tryCompare(dialog, "opened", true);
        tryCompare(dialog.editor, "loading", false);
    }

    function test_marksAndKeys() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = openKidA(win);
        const page = lib.albumPage;
        const dialog = win.tagEditorDialog;

        // v marks the cursor row; Esc clears marks before leaving the album.
        act(win, KeyRouter.ToggleMark);
        act(win, KeyRouter.MoveDown);
        act(win, KeyRouter.ToggleMark);
        compare(page.markedRows, [0, 1]);
        compare(findChild(page, "editTagsButton").text, "Edit tags (2)…");
        shot(win, "album-marked");
        act(win, KeyRouter.ToggleMark);
        compare(page.markedRows, [0]);
        act(win, KeyRouter.Back);
        compare(page.markedRows, []);
        compare(lib.content, LibraryView.Album);

        // e: the cursor track.
        act(win, KeyRouter.MoveUp);
        act(win, KeyRouter.EditTags);
        waitLoaded(dialog);
        compare(dialog.editor.trackCount, 1);
        const title = findChild(dialog, "titleField");
        verify(title.visible);
        compare(title.text, "Everything in Its Right Place");
        compare(findChild(dialog, "albumField").text, "Kid A");
        compare(findChild(dialog, "trackNumberField").text, "1");
        compare(dialog.changes(), {});
        shot(win, "tag-editor-single");
        // Esc closes the dialog only: the router leaves keys alone while it's open.
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "visible", false);
        compare(lib.content, LibraryView.Album);
        compare(win.currentScreen, Main.LibraryScreen);

        // E: all tracks (02.flac isn't on disk, so it's reported unreadable).
        act(win, KeyRouter.EditAllTags);
        waitLoaded(dialog);
        compare(dialog.editor.trackCount, 2);
        verify(dialog.multiple);
        verify(!findChild(dialog, "titleField").visible);
        verify(!findChild(dialog, "trackNumberField").visible);
        compare(dialog.editor.errorString, "1 of 2 files can't be read.");
        compare(findChild(dialog, "artistField").text, "Radiohead");
        shot(win, "tag-editor-multi");
        dialog.close();
        tryCompare(dialog, "visible", false);
    }

    function test_saveThroughDialog() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = openKidA(win);
        const page = lib.albumPage;
        const dialog = win.tagEditorDialog;
        act(win, KeyRouter.Activate); // play track 1: the queue and now-playing follow edits
        tryCompare(Playback, "title", "Everything in Its Right Place");

        act(win, KeyRouter.EditTags);
        waitLoaded(dialog);
        const title = findChild(dialog, "titleField");
        title.text = "Right Place (edited)";
        findChild(dialog, "yearField").text = "2001";
        compare(dialog.changes(), { title: "Right Place (edited)", year: "2001" });
        mouseClick(findChild(dialog, "saveButton"));
        tryCompare(dialog, "visible", false);
        compare(Library.statusMessage, "Saved tags");
        tryCompare(Playback, "title", "Right Place (edited)");
        compare(Playback.queue.count, 2);
        tryVerify(() => page.tracksModel.get(0).title === "Right Place (edited)");
        tryCompare(page, "shownYear", 2000); // the album's earliest year (02 still says 2000)

        // Put it back; Enter in a field saves.
        act(win, KeyRouter.EditTags);
        waitLoaded(dialog);
        compare(findChild(dialog, "titleField").text, "Right Place (edited)");
        findChild(dialog, "yearField").text = "2000";
        const field = findChild(dialog, "titleField");
        field.text = "Everything in Its Right Place";
        field.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(dialog, "visible", false);
        tryCompare(Playback, "title", "Everything in Its Right Place");
    }

    function test_invalidNumberKeepsDialogOpen() {
        const win = createTemporaryObject(mainComp, tc);
        openKidA(win);
        const dialog = win.tagEditorDialog;
        act(win, KeyRouter.EditTags);
        waitLoaded(dialog);
        verify(!dialog.editor.apply({ discNumber: "5000" }));
        compare(findChild(dialog, "tagError").text, "Disc number must be a number from 0 to 999.");
        verify(dialog.visible);
        dialog.close();
        tryCompare(dialog, "visible", false);
    }

    // Renaming a whole album moves its tracks to a new album id: the page follows them.
    function test_followsRenamedAlbum() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = openKidA(win);
        const oldId = lib.albumId;
        lib.tagsSaved(oldId + 1000, 1); // only some tracks edited: stay
        compare(lib.albumId, oldId);
        lib.tagsSaved(0, 2); // tracks ended up in several albums: stay
        compare(lib.albumId, oldId);
        lib.tagsSaved(oldId + 1000, 2);
        compare(lib.albumId, oldId + 1000);
    }
}
