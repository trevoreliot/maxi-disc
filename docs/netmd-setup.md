# NetMD MiniDisc recorders

music-md-manager talks to NetMD recorders (Sony MZ-N/NE/NF series, e.g. the MZ-NE410, Sharp
IM-DR, Aiwa AM-NX) over USB with libnetmd (`third_party/libnetmd`). Plug the recorder in with a
disc inserted and pick it under **Export → Available Devices**.

## Permission (udev rule)

A normal user may not open the USB device until a udev rule allows it. If the app says
"No permission to use the recorder", install the rule and plug the recorder in again:

```sh
sudo install -Dm644 docs/udev/70-netmd.rules /etc/udev/rules.d/70-netmd.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The rule tags NetMD devices with `uaccess`, so whoever is logged in at the seat gets access (no
group needed). If you installed Web MiniDisc Pro's rule before, you already have it.

## One program at a time

Only one program can hold the recorder. Close Web MiniDisc Pro (or its browser tab) while
music-md-manager uses it; the app shows "in use by another program" otherwise. The app only
holds the recorder while it's selected on the Export screen.

## Recording (SP, LP2, LP4)

Select the recorder on the Export screen, add music with `i` (here or on the Library screen; it
goes to "To record"), pick the mode under **MD options** (SP / LP2 / LP4, or `M` to cycle), check
it fits, and press `S` (or **Record**). SP: each track is decoded to CD-quality PCM and the
recorder encodes it. LP2 (132 kbps) / LP4 (66 kbps): the app encodes ATRAC3 itself with the
bundled atracdenc (built with the app; `MMM_ATRACDENC=/path/to/atracdenc` uses another), so a
track takes a little longer to prepare. The disc's track list updates after each track. **Cancel** stops after the track being recorded: a NetMD upload can't be
stopped part-way. Don't unplug during a recording: the track in progress is lost.

## Arranging, deleting, wiping

In the disc's track list ("On disc"): `J` / `K` move the track under the cursor down / up a
place, `v` marks tracks, `d` (or `x` / Delete, or **Delete**) deletes the marked tracks (else the
one under the cursor) after asking (`y` / Enter yes, `n` / Esc no), and `D` (or **Wipe disc…**)
erases the whole disc after you type `WIPE`. Esc clears the marks. The recorder keeps groups in the
disc title and doesn't update them itself: the app rewrites them after each move or delete (a
track moved past a group's edge leaves it; moved into a group's middle, it joins).

## Titles

MiniDiscs store plain ASCII titles. The app replaces what a disc can't show ("Björk – Jóga"
becomes "Bjork - Joga") and shows the result before writing. The disc title and the group names
share one header of at most 255 characters.

## Troubleshooting

- Start the app with `MMM_NETMD_DEBUG=1` to log every USB exchange with the recorder (libnetmd's
  log), plus `QT_LOGGING_RULES="mmm.netmd.debug=true"` for the app's side.
- `lsusb | grep -i sony` should list the recorder (`054c:00ca` for the MZ-NE410).
