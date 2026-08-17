# Advanced AutoDeafen

A [Geode](https://geode-sdk.org) mod for Geometry Dash that deafens you on
Discord when a run passes a configured percent (per level, per startpos) and
un-deafens when the attempt ends. No Discord keybind needed.

Grab the latest `.geode` from the
[releases page](https://github.com/zoinkdoink/autodeafen/releases) and drop it
into `<Geometry Dash>/geode/mods/`.

Turn off any other auto-deafen (standalone mods, or the auto-deafen features
in mod menus like MegaHack/Eclipse): multiple deafeners fight over your Discord
state and double-toggle you.

## Two delivery modes

Pick one with the `delivery-mode` setting. Neither is "primary" — they have
different tradeoffs:

**`direct`** — talks to Discord's **local RPC IPC channel** (named pipe on
Windows, `discord-ipc-N` unix socket elsewhere) and sets your voice state with
`SET_VOICE_SETTINGS { deaf }`. Works on all three platforms, reads your real
deafen state (so it never toggles you the wrong way, and only un-deafens what
it deafened), and needs no keybind. Cost: a one-time OAuth authorization that
Discord gates (see below). On Linux the mod reaches the host socket via the
Wine named pipe, AF_UNIX (Wine Staging 10.2+), or a bundled auto-launched
[wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
(MIT, by 0e4ef622) for stock Proton.

**`keybind`** — taps a key you've bound to Toggle Deafen in Discord, via
`SendInput`. No Discord authorization at all, works for anyone. Cost:
**native Windows only** (desktop Discord on macOS reads keybinds at the raw HID
level, unreachable by synthetic events — verified against `CGEventPost` and
`CGEventPostToPid`; Wine-internal input never reaches native Linux apps), and
it can't read state, so it assumes nothing else touches your deafen key
mid-session.

Un-deafen (either mode) fires on death, completion, restart, or quit — and
optionally while paused.

### The direct-mode authorization gate

Discord restricts the `rpc` OAuth scope (required for `SET_VOICE_SETTINGS`) to
an application's **owner and whitelisted users** unless the app is approved for
general RPC access. `rpc.voice.write` is not valid on its own — it only works
alongside `rpc` — so there is no public sub-scope that avoids the gate (tested
against both the RPC and browser authorize paths). So for this app's built-in
ID, `direct` works for the owner and up to 50 **App Testers** (Discord
Developer Portal → your app → App Testers).

Any user can get `direct` without the whitelist by creating their **own**
Discord application (they're then its owner). Otherwise, `keybind` mode needs
no authorization at all.

#### Using your own Discord app

1. Go to the [Discord Developer Portal](https://discord.com/developers/applications)
   and click **New Application**. Give it any name and create it.
2. Open the **OAuth2** tab (left sidebar). Turn on **Public Client** and save —
   required, because the mod exchanges the auth code with PKCE and no secret.
3. Still on the **OAuth2** tab, under **Redirects**, click **Add Redirect**,
   paste the URL below, and save:
   ```
   http://127.0.0.1:53535
   ```
4. Open the **General Information** tab and copy the **Application ID**.
5. In Geometry Dash, open the mod's settings (Geode → Advanced AutoDeafen →
   gear icon), find **Discord app ID (advanced)**, and paste the ID there.
6. Back on the mod's **Discord connection** row, click **Log out** (if shown),
   then **Connect**, and authorize.

## Configuring levels

Pause in any (non-platformer) level and hit the AutoDeafen button in the pause
menu: an enable toggle for the level, plus one percent input per spawn point
(level start and each startpos, labeled with where it sits in the level; the
one you're paused on is highlighted). Empty input = no deafen from that spawn
point — the mod is strictly opt-in and does nothing on unconfigured levels. A
threshold at or below the spawn percent never fires.

Configs persist in the mod's save directory as `levels.json` (keys: `o:<online
id>` or `l:<level name>`; `sp` maps startpos index → absolute percent, index 0
= level start), re-read on level enter, so they're also hand-editable.

## Development

```sh
# mod (requires Geode SDK + CLI)
cd mod && geode build

# pure-core C++ tests: thresholds, ipc framing, sha256/pkce
clang++ -std=c++20 -o /tmp/core_tests mod/test/core_tests.cpp && /tmp/core_tests
```


`.clangd` points at `mod/build/compile_commands.json` for LSP support. Windows
builds cross-compile from Linux per the [Geode
docs](https://docs.geode-sdk.org/getting-started/cpp-stuff/), or via the
`geode-sdk/build-geode-mod` GitHub Action.
