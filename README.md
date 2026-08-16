# AutoDeafen

A [Geode](https://geode-sdk.org) mod for Geometry Dash that deafens you on
Discord when a run passes a configured percent (per level, per startpos) and
un-deafens when the attempt ends. No Discord keybind needed.

Works on **Windows, macOS, and Linux (Wine/Proton)** — validated on real
hardware on all three. Grab the latest `.geode` from the
[releases page](https://github.com/zoinkdoink/autodeafen/releases) and drop it
into `<Geometry Dash>/geode/mods/`.

## How it works

The mod watches your percent each frame and, when an armed threshold is
crossed, talks to Discord's **local RPC IPC channel** (named pipe on Windows,
`discord-ipc-N` unix socket elsewhere) and sets your voice state directly:
`SET_VOICE_SETTINGS { deaf }`. Auth is a one-time "Authorize" consent (OAuth +
PKCE, no secrets, `rpc`/`rpc.voice.write` scopes) — a popup inside Discord, or
the browser on Discord builds that reject the in-app route:

- **Windows / macOS**: install the mod, click Authorize once (settings page →
Discord connection → Connect).
- **Linux (Wine/Proton)**: same flow. The mod reaches the host's socket via the
Wine named pipe, directly over AF_UNIX (Wine Staging 10.2+), or — the common
stock-Proton case — by auto-launching a bundled copy of
[wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
(MIT, by 0e4ef622) inside the prefix, tied to the game's lifetime. No host-side
setup either way.

It reads your current deafen state first, never "deafens" someone already
deafened, and only un-deafens if it was the one that deafened you. Un-deafen
fires on death, completion, restart, or quit — and optionally while paused.

## Keybind fallback mode

For native **Windows** setups without a usable RPC server (browser Discord,
Vesktop without full RPC, or if you decline the OAuth), set `delivery-mode` to
`keybind`: the mod taps a configurable key that you bind to Toggle Deafen in
Discord via `SendInput`, no setup needed.

Windows only in practice: desktop Discord on macOS reads global keybinds at the
raw HID device level, which synthetic events cannot reach (verified against
both `CGEventPost` and `CGEventPostToPid`), and Wine-internal input never
reaches native Linux apps. Both platforms use the direct mode.

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
