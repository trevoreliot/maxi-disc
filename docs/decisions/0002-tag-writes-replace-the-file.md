# 0002 — Tag edits write a copy and swap it in, instead of editing the file in place

- **Status:** accepted and implemented (2026-11-21, M6): `src/core/tagwriter.cpp`
  (`TagWriter::write`); test `tst_tagwriter` replacesAtomically
- **Decided by:** Claude Code during M6, as a default; the user can revisit
- **Revisit if:** hard-linked libraries (or file ownership/xattrs) matter, editing gets slow on
  a filesystem without reflinks, or an external tool watching the files relies on the inode

## Context

TagLib saves by rewriting the file in place. When the new tags don't fit in the existing
padding (common for FLAC/MP3 after adding a long comment or more fields), it shifts the whole
audio stream inside the same file. Two problems with that for us:

1. **The track may be playing.** GStreamer's `filesrc` has the file open and reads it as it
   goes; if the bytes move under it, playback glitches or the decoder errors (and our stall
   recovery would kick in). Editing the current track's tags is a normal thing to do.
2. **A crash, a full disk or a yanked USB drive mid-save leaves a half-written audio file.**

## Decision

`TagWriter::write()` copies the file to a hidden temp file in the same folder
(`.mmm-edit-XXXXXX.<ext>`; `QFile::copy` uses a reflink where the filesystem supports it,
e.g. btrfs/XFS, so it's instant there), writes the tags into the copy with TagLib, and then
`rename()`s the copy over the original. `rename()` within a folder is atomic: other readers
see either the old file or the new one, and an open file descriptor keeps reading the old data.
The library scanner skips hidden files, so it never picks up the temp file.

## Costs

- **The inode changes.** A hard link to the file keeps the old tags (it becomes a separate
  file). Owner/group (if someone else owns it) and extended attributes aren't carried over;
  permissions are.
- **Needs space for one extra copy** of the file being edited (none with reflinks), and write
  permission on the folder, not just the file.
- **Slower without reflinks**: a full copy per file (~30 MB per FLAC track). Fine for album
  edits on an SSD; noticeable for hundreds of tracks on a slow drive.

## How to revisit

Switch `TagWriter::write()` to open the original with TagLib and `save()` it directly (the
`applyChanges()` helper already takes a path), maybe only when the folder isn't writable or the
file has more than one link (`st_nlink > 1`). Keep `replacesAtomically` as the test of whichever
behaviour is chosen.
