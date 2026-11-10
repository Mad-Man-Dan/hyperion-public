# The music player

## Lede

Hyperion has a music player you call up over a game with Ctrl+F9. It plays the game's own
soundtrack, your own music files, or what your other apps are playing, in three windows: the
player, an equalizer and a playlist. The windows take on the style of the HUD you play with.

## How to use it

### Calling it up

- In a game, press **Ctrl+F9**. The three windows appear over the game. Press it again, or
  Escape while the player has the focus, to put them away. The music keeps playing.
- The player works only in games. In the menus the key does nothing.
- To change the key: Settings, Hotkeys, under **Addons**, Music player.
- To turn the player off: Settings, Sound, **Music player** off. The key then does nothing and
  the game's music plays as in the original.

### What it plays

Choose a source with the **Src** button on the main window, the L key, or the player's menu,
Source:

- **The game's music**: StarCraft's or Brood War's soundtrack, following the race you play as
  the original does. Where your install is StarCraft: Remastered, it plays Remastered's
  recordings, and each race's fourth track is listed too. Radio Free Zerg is listed in Brood War
  games.
- **My music**: your own MP3, Ogg Vorbis, FLAC and WAV files. The list starts with the `music`
  folder in Hyperion's Data folder. **Add** in the playlist brings in more files or a whole
  folder. Titles and artists come from the files' tags. **List**, Save the list writes
  `My list.m3u8` into the music folder.
- **Playing on this computer**: what your other apps play (below).

### With the game's music

The player's menu, Options, **In games** has three choices:

| Choice | What you hear |
|---|---|
| This player instead of the game's music (the default) | the game's music stays off while the player has something to play |
| Both, the player over the game's music | the game's music underneath, at its own volume |
| The game's music | the player stays quiet once a game starts |

The player also follows the game:

- it lowers itself under unit speech (Options, **Lower under unit speech**, on by default);
- it can pause through cinematics and briefings (Options, **Pause for cinematics and briefings**);
- when a game ends it pauses, and plays on from there in your next game.

### The windows

- **The main window**: the track's title, the time, the visualizer, the volume and balance, the
  transport buttons, shuffle and repeat. A click on the time switches elapsed and remaining.
- **The equalizer**: ten bands from 60 Hz to 16 kHz and a preamp, with **Auto** and presets
  (Flat, Rock, Classical, Full bass, Laptop speakers and Live), or Reset to flat.
- **The playlist**: the tracks, a click to choose one and a double click to play it. The mouse
  wheel scrolls it three rows a notch. Add, Remove, Select and Misc (sort by title, shuffle the
  list) are at its foot.
- **The visualizer**: a spectrum or an oscilloscope, or off (the menu's Visualizer).

Each window:

- **docks**: windows snap together and to the screen's edges (Options, Snap windows together),
  and the main window carries the windows docked to it;
- **shades**: the shade button, a double click on its title bar or the menu folds it to a bar,
  and the windows docked under it move up with it;
- **sizes**: the menu's Size, from 75% to 200% (100% is the default) and **Double size** (D);
- **remembers** where it was, its size and its shade, between games and between runs.

Options also has **Always on top in games**, which the clutterbar's A button switches too. In
this version the windows stay over the game either way.

The player's menu opens from the mark at the left of a title bar, a right click on a window, the
Menu key or Shift+F10. A right click on a playlist row opens that row's menu. Every button and
menu item has a tooltip, and a greyed one says why.

### Keys

These work while the player has the focus (a click on one of its windows gives it the focus; a
click on the game takes it back):

| Key | Does |
|---|---|
| Z, X, C, V, B | previous, play, pause, stop, next |
| Left, Right | back or forward 5 seconds |
| Up, Down | volume |
| S, R | shuffle, repeat |
| L | the source menu |
| D | Double size |
| Alt+G, Alt+E | the equalizer, the playlist |
| Enter, Delete, Ctrl+A | play the selected track, remove the selected tracks, select all |
| Menu, Shift+F10 | the player's menu |
| Escape | closes a menu, else puts the player away |

### Playing on this computer

On Windows the player can show and control what your other apps play: Spotify, a browser tab
playing music or a video, the Apple Music app, or any app that Windows' own media controls show.

- The playlist becomes the list of apps, each with its track and cover. A click on one shows it.
- **Follow what plays** (on by default) keeps the player on whichever app played last, as
  Windows' media flyout does. Choosing an app turns it off.
- Play, pause, stop, previous, next, seeking and each app's own volume work from the player,
  as far as the app allows. What an app does not allow is greyed, and the tooltip says so.
- The visualizer moves to the app's sound. The sound itself stays the app's: Hyperion plays
  none of it, so the equalizer, shuffle, repeat and balance are off for apps.

**iTunes.** iTunes for Windows is listed too while it is open, last among the apps. Its
playlists follow, under **iTunes playlists**: Music (your whole library) and your own playlists.
A click opens one, with its tracks and the one iTunes is playing lit. A double click on a track
plays it in iTunes, Apple Music's tracks included. A playlist's right-click menu plays it from
the start. **Back** returns to the playlists. Hyperion connects to iTunes only while it is
open, and lets go when you quit iTunes, so iTunes never asks you about another program.

## Limits and known issues

- The player is in games only; it is not in the menus.
- Playing on this computer is Windows only for now. On Linux and the Mac that source says "Not
  available yet".
- The browser build has no music player.
- The visualizer needs Windows 10 version 2004 or later for other apps' sound. Where Windows
  refuses, it stays still and the player says so.
- Shuffle, repeat, the balance and the equalizer do not apply to other apps, which keep their own.

## In progress and planned

- Other apps on Linux, through Linux's media controls (planned).
- An Addons tab in the Mods screen and an entry in the game's menu; until then the player's
  switch is in Settings, Sound, and the key is the way in (planned).

## Technical notes

- The player and its lists: `engine/src117/port/addons/port_music.cpp`; its state for the windows
  and the live channel: `port_musicmodel.h`.
- Decoding: dr_mp3, dr_flac and dr_wav from the disk a piece at a time, stb_vorbis for Ogg
  (`port_musicdecode.cpp`), on a thread of its own, into a ring of three quarters of a second.
- The sound reaches the mixer as a source of Hyperion's own (`PortSoundSource`,
  `port/data/port_dsound.cpp`). The equalizer is ten bands as biquad filters, applied
  on the mixer's thread with the volume, balance and the lowering under speech.
- The game's music is silenced while the player plays in its place (`lang/music.cpp`,
  `PortMusicGameHeard`), and 1.17's track changes move the player to the same track.
- The windows are an overlay layer over the game (`port_musicview.cpp`). To the game they are
  console, like a HUD's panels: the game keeps its cursor and a click reaches no unit under them.
- The skin comes from the HUD style's `musicPlayer` block (`engine/docs/hud-format.md`, "The
  music player"); its plates are drawn by the HUD's shape shader under the windows.
- Other apps: Windows' media session manager (`Windows.Media.Control`) on a thread of its own,
  each app's volume through its audio session, the visualizer through process loopback
  (`port_musicsessions.cpp`). iTunes through its COM interface, reached only while
  `iTunes.exe` runs (`port_musicitunes.cpp`).
- Settings: `hyperion.musicPlayer` in `settings.json`; the player's lists, options, equalizer and
  windows in `music.json` in the data folder.
- The full module doc: `engine/docs/modules/addons.md`.
