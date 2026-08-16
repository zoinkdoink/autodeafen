# AutoDeafen

Deafens you on Discord when your run passes a configured percent (<cy>per
level, per start position</c>) and un-deafens when the attempt ends. On Windows,
you can use the keybind mode to trigger the deafen keybind. On Linux and
MacOS, the keybind can't be triggered, so the mod talks to your Discord client
directly.

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

The mod is strictly opt-in: it does nothing on levels you haven't configured,
never deafens you if you already deafened yourself, and only un-deafens what
it deafened.

## Platforms

- <cg>Windows & macOS</c>: fully supported.
- <cg>Linux (Wine/Proton)</c>: supported — the mod reaches your host Discord
  automatically, bundling
  [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  for stock Proton.

There is also a Windows-only <cy>keybind mode</c> for setups without the
Discord desktop app: the mod taps a key you've bound to Toggle Deafen.

## Credits

- [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge)
  by 0e4ef622 (MIT), bundled for Linux support.
