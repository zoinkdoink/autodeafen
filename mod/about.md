# Advanced AutoDeafen

Deafens you on Discord when your run passes a configured percent (<cy>per
level, per start position</c>) and un-deafens when the attempt ends.

Use the default <cg>direct</c> mode on every platform: it talks to your
Discord client directly. No keybind to set up, and it knows your real deafen
state so it never toggles you the wrong way. The <cy>keybind</c> mode is a
Windows-only mode for normal keybind autodeafen.

<cr>Turn off any other auto-deafen you have</c>, as other auto-deafen mods and
the auto-deafen features in mod menus like <cy>MegaHack</c> or <cy>Eclipse</c>
will fight over your Discord state and double-toggle you.

## Direct Mode Setup (once)

1. Have the Discord <cy>desktop app</c> running.
2. Open this mod's settings and click <cg>Connect</c>.
3. Click <cg>Authorize</c> in the popup that appears <cl>inside Discord</c>.

The connection resumes automatically from then on.

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

- <cg>Windows & macOS</c>: fully supported.
- <cg>Linux (Wine/Proton)</c>: supported. the mod reaches your host Discord
  automatically, bundling
  [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  for stock Proton.

There is also a Windows-only <cy>keybind mode</c> for setups without the
Discord desktop app: the mod taps a key you've bound to Toggle Deafen.

## Credits

- [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  by 0e4ef622 (MIT), bundled for Linux support.
