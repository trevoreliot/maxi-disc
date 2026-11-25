# 0008: ListenBrainz instead of Last.fm, the token in the keyring

## Context

M14 was planned as Last.fm scrobbling and was first built that way (the user's own API key and
shared secret, Last.fm's desktop auth flow). Before it shipped, Last.fm announced that from
1 December 2026 holding an API key requires a Last.fm Pro subscription: new keys need Pro, and
keys without it stop working. That falls on whoever owns the key: in a bring-your-own-key app,
every user. A project-wide key would put it on the project, and the project's rule is that keys
and secrets are never committed (the repository is public). The user didn't want to pay for Pro
and asked for Last.fm to be dropped from the app entirely, in favour of ListenBrainz.

## Evidence

- Last.fm's announcement, "API keys will require Pro from 1st December" (support.last.fm, topic
  121030, 18 Nov 2026): "Creating a new key will require a Pro account, and existing keys without
  it will stop working until you subscribe." Widely used scrobblers get partner keys
  (partners@last.fm); a free tier for self-hosted scrobblers was requested but not offered.
- ListenBrainz (MetaBrainz, the MusicBrainz people; free, open source, open data) authenticates
  with a user token copied from listenbrainz.org/settings: no app registration, no secret, no
  browser round trip. `POST /1/submit-listens` takes `playing_now` (one, no timestamp), `single`
  (one) or `import` (up to 1000) listens as JSON; `GET /1/validate-token` names the token's user.
  Its guideline for when a listen counts is the one Last.fm used: half the track or 4 minutes,
  whichever is lower. Clients must send at most one request a second and identify themselves in
  the User-Agent (ours already carries the project URL).
- QtKeychain 0.17 (Qt 6, Core + DBus only) stores secrets in KWallet / the Secret Service; it's
  packaged here and keeps core free of GUI code.

## Decision

Scrobbling goes to ListenBrainz only; the app has no Last.fm code. The user pastes their user
token into the ListenBrainz dialog (☰ → ListenBrainz…, or the transport bar's toggle); the app
checks it (validate-token) and keeps it in the desktop keyring through QtKeychain
(`SecretStore`, key `listenbrainz/token`). Only the user name and on/off go to QSettings. The
token is never written in plain text, put in a URL or logged. If the keyring can't be used, the
dialog says so; there's no plain-file fallback. Plays wait in `scrobbles.db` next to the library
database (table `pending_scrobbles`, migration v7, unchanged from the Last.fm build; its own file
so a long library scan never holds a listen up) until ListenBrainz has them.

A play counts after half the track or 4 minutes, and only for tracks longer than 30 s with an
artist and a title (ListenBrainz sets no minimum length; the 30 s rule keeps skits and jingles
out, as other scrobblers do).

## Revisit when

- The user wants plays on Last.fm again: ListenBrainz doesn't forward to Last.fm. Options then:
  Last.fm Pro with the user's own key, a partner key from Last.fm, or Last.fm offering a free
  tier for personal scrobblers.
- Users want another server that speaks the same API (Maloja, Koito, a self-hosted ListenBrainz):
  a "server address" setting is all it takes (PLAN follow-up).
- Users without a keyring ask for it: an opt-in, clearly labelled plain-file store.
