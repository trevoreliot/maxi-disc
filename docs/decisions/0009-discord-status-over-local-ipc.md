# 0009: Discord status over the local IPC socket, with the user's application ID

## Context

M15 shows what's playing on the user's Discord profile. Discord offers a Social SDK (closed,
C++), the older discord-rpc library (archived) and the desktop client's local IPC socket, which
every open-source music player uses. Any of them needs a Discord application ID: Discord shows
"Listening to <the application's name>". The project's rule is that Discord keys are supplied at
runtime and never committed.

## Evidence

- Discord's RPC docs: the socket is `discord-ipc-0`…`9` in `$XDG_RUNTIME_DIR` (else the temp
  dirs); frames are little-endian uint32 opcode + length + JSON; a handshake with `client_id`
  gets READY; `SET_ACTIVITY` takes Playing / Listening / Watching / Competing activities with
  `details`, `state`, `status_display_type` (which line the member list shows), `timestamps`
  (start + end draw a time bar for Listening) and `assets`. About 5 updates per 20 s.
- Flatpak / Snap Discord and Vesktop put the socket in `app/com.discordapp.Discord`,
  `snap.discord` and `app/dev.vencord.Vesktop` under the runtime dir (from widely used clients).
- The newer docs say timestamps are milliseconds; Discord's own discord-rpc library and most
  clients send Unix seconds, which the desktop client (and arRPC / Vesktop) accept.
- An application is free and instant at discord.com/developers/applications. Its ID isn't a
  secret (every client sends it to the local Discord app in the clear).
- Images must be uploaded assets or public URLs; local album art is neither, and the library
  doesn't store MusicBrainz release IDs (which Cover Art Archive URLs need).

## Decision

Talk to the local socket directly (core `DiscordIpc`, Qt's QLocalSocket; no SDK). The user
creates their own Discord application and pastes its ID in the Discord dialog; it's kept in
QSettings (`discord/appId`, with `discord/enabled`), not the keyring, since it isn't secret.
Shown while playing: type Listening, "Song - Artist" (`details`, also the one-line status in
the member list: the user's choice, `status_display_type` 2), the album under it (`state`), a
time bar from start / end in Unix seconds. Paused, stopped, switched off or quitting: cleared.
No cover art. Updates go at most 5 per 20 s, changes within
250 ms merged, a time bar off by ≤ 2 s not re-sent. If Discord refuses the ID, the app stops
retrying and says so; if Discord isn't running or restarts, it reconnects (5 s → 60 s).

## Revisit when

- Packaged builds want it to work without the user's own application: a project application ID
  set at build time (not in the repository).
- The library stores release MBIDs: Cover Art Archive URLs as the large image.
- Discord stops accepting seconds: send milliseconds.
