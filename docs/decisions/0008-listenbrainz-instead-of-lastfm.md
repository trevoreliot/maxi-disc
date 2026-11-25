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
- Users without a keyring ask for it: an opt-in, clearly labelled plain-file store. (Asked
  2026-11-24; answered with pass instead, below.)

## Revisited 2026-11-24 (M14b): pass when there's no keyring

The user's KDE Wallet is turned off (kwalletd6 exits at once, no Secret Service on the bus), so
the token couldn't be saved and ListenBrainz needed connecting again on every launch. They keep
their passwords in pass (with QtPass), and chose that over turning the wallet on or a plain file.

- `PassSecretStore` runs `pass` (`show` / `insert --multiline --force` / `rm --force`), one call at
  a time, the value on stdin (never on a command line). Entry `music-md-manager/listenbrainz/token`.
  Writing only encrypts (no passphrase); reading at launch may bring up the user's pinentry while
  plays wait in the queue; a cancelled prompt is shown as "Can't use pass: …" and the account
  waits for a reconnect, as with a locked keyring.
- Which store: the keyring if `QKeychain::isAvailable()`, else pass if it's set up (`pass` runs and
  the store has a `.gpg-id`), else the keyring (its errors say what's wrong). `isAvailable()` said
  yes on the user's machine while every call failed ("The name is not activatable"), so with pass
  set up the keyring is tried and `FallbackSecretStore` hands over to pass when it turns out
  unusable (the call is made again there; the user cancelling an unlock prompt isn't a switch,
  and a keyring that has worked once stays). QSettings
  `secrets/backend` = auto / keyring / pass overrides it (no UI); `MMM_SECRET_BACKEND` does too
  (ctest: pass with an empty store, so tests never probe the real keyring).
- No moving between stores: switching (e.g. turning the wallet back on) means connecting once more.
- Still no plain-file store.
