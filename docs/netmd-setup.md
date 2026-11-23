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

## Recording (SP)

Select the recorder on the Export screen, add albums, artists, genres or playlists with `i`
(they go to "To record"), check they fit, and press `S` (or **Record**). Each track is decoded to
CD-quality PCM and sent to the recorder, which encodes it in SP; the disc's track list updates
after each track. **Cancel** stops after the track being recorded: a NetMD upload can't be
stopped part-way. Don't unplug during a recording: the track in progress is lost.

## Titles

MiniDiscs store plain ASCII titles. The app replaces what a disc can't show ("Björk – Jóga"
becomes "Bjork - Joga") and shows the result before writing. The disc title and the group names
share one header of at most 255 characters.

## Troubleshooting

- Start the app with `MMM_NETMD_DEBUG=1` to log every USB exchange with the recorder (libnetmd's
  log), plus `QT_LOGGING_RULES="mmm.netmd.debug=true"` for the app's side.
- `lsusb | grep -i sony` should list the recorder (`054c:00ca` for the MZ-NE410).
