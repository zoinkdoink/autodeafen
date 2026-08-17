# Advanced AutoDeafen

Deafens you on Discord when your run passes a configured percent (<cy>per
level, per start position</c>) and un-deafens when the attempt ends.

<cr>Turn off any other auto-deafen you have</c>, as other auto-deafen mods and
the auto-deafen features in mod menus like <cy>MegaHack</c> or <cy>Eclipse</c>
will fight over your Discord state and double-toggle you.

## Two ways to deafen

Set <cy>Delivery mode</c> to whichever works for you:

**<cg>direct</c>**: talks to your Discord client directly. No keybind to set
up, and it reads your real deafen state so it never toggles you the wrong way.
Works on Windows, macOS, and Linux. Requires a one-time Discord authorization
that Discord only grants to an app's owner and whitelisted testers, so out of
the box it works for this mod's developer and invited testers, or for anyone
using their own Discord app (see below).

**<cy>keybind</c>**: taps a key you've bound to <cy>Toggle Deafen</c> in
Discord. No Discord authorization at all, works for anyone, but only on
<cy>native Windows</c> (the keypress can't reach Discord on macOS or
Linux).

## Direct mode setup (once) (Windows, Mac, and Linux)

<cr>Note:</c> Discord only allows this for the mod developer and invited
testers. If that's not you, the steps below will fail with an <cy>invalid
scope</c> error and you'll need to make your own Discord app first (guide
linked at the bottom), or just use <cy>keybind</c> mode.

1. Have the Discord <cy>desktop app</c> running.
2. Open this mod's settings, set <cy>Delivery mode</c> to <cy>direct</c>, and
   click <cg>Connect</c>.
3. Click <cg>Authorize</c> in the popup that appears <cl>inside Discord</c>.

The connection resumes automatically from then on.

[**Own-app setup guide →**](https://github.com/zoinkdoink/autodeafen#using-your-own-discord-app)

## Keybind mode setup (once) (Windows Only)

1. Set <cy>Delivery mode</c> to <cy>keybind</c>.
2. Bind a key to <cy>Toggle Deafen</c> in Discord's settings.
3. Set the same key in this mod's <cy>Deafen key</c> setting.

## Usage

Pause in any classic level and hit the <cy>AutoDeafen button</c> in the pause
menu: enable the level, then set a deafen percent next to any start position
(the one you're paused on is <cy>highlighted</c>). Leave an input empty for no
deafen from that spawn point.

Cross the percent mid-run and you're deafened; die, finish, or quit and you're
un-deafened. Extra behavior (un-deafen while paused, practice mode) lives in
the settings.

The mod is strictly opt-in: it does nothing on levels you haven't configured.

## Platforms

- <cg>Windows</c>: both modes.
- <cg>macOS</c>: direct mode.
- <cg>Linux (Wine/Proton)</c>: direct mode; the mod reaches your host Discord
  automatically, bundling
  [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  for stock Proton.

## Credits

- [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  by 0e4ef622 (MIT), bundled for Linux support.
