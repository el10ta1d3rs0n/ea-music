# 🎵 EA-MUSIC

> ### ⚠️ A POLITE BUT VERY FIRM NOTICE TO AOIZEN FANS
>
> I regret to inform you, with all due respect, that **Aoizen fans are NOT
> allowed to use this project.** I’m sorry. Actually, I’m not. I’m a tiny bit
> mad about it. Please, I beg you, go listen to something else. I mean this
> with the utmost politeness and a small but extremely noticeable amount of
> rage. 🙏🔥
>
> Thank you for your understanding. I appreciate it. I’m still a little mad.
> I’ll get over it. Maybe.

---

## What is this?

**EA-MUSIC** is my minimalist, no-bloat, keyboard-first YouTube Music player for
Linux. I built it with [raylib](https://www.raylib.com/). It searches and
streams via `yt-dlp`, plays via `mpv`, and wastes exactly zero of my CPU cycles
on things I didn’t ask for.

It has a search page, a library system, a player bar, and a menu. That’s it.
That’s the whole product. No telemetry. No “AI-powered recommendations.” No
subscription upsell. No 400 MB of Chromium baked into a binary. Just sound.

You’re free to use it, fork it, break it, fix it, rebrand it, ship it, or feed
it to your cat. Go wild.

---

## Why I made this

Because every music player on Linux seems to be one of these:

- A 300 MB Electron app that takes 12 seconds to cold-start.
- A terminal player with 47 keybindings and a config file in a language you
  have to learn.
- Something that *almost* works but insists on managing your library for you.

I wanted something else: a **thin, sharp, fast tool** that does one thing well.
It streams YouTube Music, remembers your libraries, and gets out of your way.

I’ve looked around, and to the best of my knowledge, no other project does
exactly this in exactly this way. I’d love to be proven wrong. Please prove me
wrong. (I’m also a little mad that nobody else thought of this first.)

---

## My philosophy

- **Minimalism.** If a feature doesn’t directly serve “find song, play song,”
  it doesn’t ship.
- **Unix philosophy.** Small tools, composed well. `yt-dlp` finds. `mpv` plays.
  EA-MUSIC glues. That’s it.
- **Speed.** Cold start is measured in milliseconds, not seconds. Rendering is
  60 FPS on integrated graphics. Memory use is tiny.
- **No limits.** No account, quota, or free-tier cap on libraries, songs per
  library, or searches per day. The only limits are your network and your
  patience.
- **Keyboard-first.** The mouse works, but you don’t need it. Every meaningful
  action has a key.
- **No telemetry.** I don’t know who you are. I don’t want to know. I’m happier
  this way.

---

## Features

- 🔍 YouTube Music search via `yt-dlp` (async, non-blocking)
- 📚 Unlimited libraries — each one is a plain-text `.lib` file in
  `~/.config/ea-music/` or `$XDG_CONFIG_HOME/ea-music/`
- 💾 Add any search result to any library with two keystrokes
- 🔁 Repeat All per player, saved between runs
- 🔊 Volume control from 0–200%, saved between runs (debounced)
- 🎚️ Toggleable loudness normalization (EBU R128 via `loudnorm`)
- ⏯️ Seek, pause, loop the current song, and mute — all keyboard-driven
- 🗂️ Create, rename, and delete libraries from inside the app
- 🎨 Fully keyboard-navigable — no mouse required
- 🖋️ Font fallback chain: JetBrains Mono → Inter → DejaVu → Noto → raylib
  default. Add `fonts/ui.ttf` to brand your build.

---

## Shortcuts

### Global (anywhere in the app)

| Key | Action |
|---|---|
| `Ctrl+1` | Go to Home |
| `Ctrl+2` | Go to Search |
| `Ctrl+3` | Go to Library |
| `Ctrl+4` | Open “New library” dialog |
| `Ctrl+Tab` | Cycle pages forward |
| `Ctrl+Shift+Tab` | Cycle pages backward |
| `Space` | Play / Pause |
| `S` | Stop playback (clears playback source) |
| `M` | Mute / unmute |
| `L` | Toggle loop-current-song |
| `N` | Toggle audio normalization |
| `←` | Seek −5 seconds |
| `→` | Seek +5 seconds |
| `↑` | Volume +5% (when not navigating a library) |
| `↓` | Volume −5% (when not navigating a library) |
| `Esc` | Close / go back |

### Search page

| Key | Action |
|---|---|
| `Enter` | Run the search |
| `Shift+Enter` | Play the highlighted result |
| `Ctrl+A` (no results selected) | Add highlighted result to a library |
| `↑` / `↓` | Navigate results |
| `Ctrl+A` (in the box) | Clear the search box |
| `Ctrl+V` (in the box) | Paste from clipboard |
| `Backspace` / `Delete` | Delete characters (with repeat) |
| Mouse wheel | Scroll results |
| Click row | Play result |
| Click **Add** button | Add result to a library |

### Library list page

| Key | Action |
|---|---|
| `↑` / `↓` | Navigate libraries |
| `Enter` | Open selected library |
| `R` | Rename selected library |
| `D` / `Delete` | Delete selected library |
| Click card body / **Open** | Open library |
| Click **Rename** | Rename library |
| Click **Delete** | Delete library |

### Inside a library

| Key | Action |
|---|---|
| `↑` / `↓` | Navigate songs |
| `Enter` | Play selected song |
| Click row (once) | Select song |
| Click row (twice) | Play song |
| `D` / `Delete` | Delete selected song |
| `R` | Toggle **Repeat All** |
| `Esc` | Back to library list |
| Mouse wheel | Scroll song list |
| Click **Repeat All** button | Toggle Repeat All |

### “Create a library” dialog

| Key | Action |
|---|---|
| Type | Enter library name (blocks `/ \ : * ? " < > |`) |
| `Ctrl+A` | Clear input |
| `Backspace` / `Delete` | Delete a character |
| `Enter` | Create library |
| `Esc` | Cancel |
| Click **Create** / **Cancel** | Same as above |

### “Add song to library” modal

| Key | Action |
|---|---|
| `↑` / `↓` | Navigate libraries |
| `Enter` | Add to selected library |
| Click a library | Add to that library |
| Mouse wheel | Scroll library list |
| `Esc` | Cancel |

### “Rename library” dialog

| Key | Action |
|---|---|
| Type | Enter new name (dotfiles and path separators are blocked) |
| `Backspace` | Delete a character |
| `Enter` | Confirm rename |
| `Esc` | Cancel |

### Player bar (when visible)

| Key / Action | Result |
|---|---|
| Click **Play/Pause** | Toggle pause |
| Click **Stop** | Stop playback |
| Click **Loop** | Toggle loop-current-song |
| Click **speaker icon** | Toggle mute |
| Drag **timeline** | Seek |
| Drag **volume slider** | Change volume |
| Mouse wheel over volume | Nudge volume ±5% |

---

## Dependencies

**Runtime:**

- `raylib` (graphics, input, and font rendering)
- `mpv` (playback)
- `yt-dlp` (search and stream resolution)
- `glibc`, `gcc-libs`
- `mesa` and `libgl` (OpenGL)
- X11 libraries: `libx11`, `libxrandr`, `libxi`, `libxcursor`, `libxinerama`,
  `libxext`
- `sdl2` (bundled for completeness; raylib uses GLFW by default)
- `xorg-xwayland` (for Wayland sessions running the X11 build)
- **Fonts (any one is enough; all four are recommended):**
  - `ttf-jetbrains-mono`
  - `ttf-inter`
  - `ttf-dejavu`
  - `noto-fonts`

**Optional:**

- `sdl3` — if you rebuild raylib against the SDL3 backend
- `fonts/ui.ttf` bundled in the repo — overrides the other fonts

**Build tools:**

- `gcc`, `make`, `pkgconf`, `git`

---

## Installation (Arch Linux)

You can install EA-MUSIC in either of these ways:

### Build from source

```bash
git clone https://github.com/el10ta1d3rs0n/ea-music.git
cd ea-music
make
sudo make install
```

### Install with the PKGBUILD

```bash
git clone https://github.com/el10ta1d3rs0n/ea-music.git
cd ea-music
makepkg -si
```

### Use a release binary

Download the Linux binary from the project’s **Releases** page, then follow any
release-specific instructions for making it executable and running it.

---

## Building from source (other Linux distributions)

You’ll need raylib, mpv, yt-dlp, and a C11 compiler with pthreads installed.
Then run:

```bash
make
./ea-music
```

**Platform support:** Linux only. The backend uses `prctl(PR_SET_PDEATHSIG)`,
Unix-domain sockets, `fork`/`exec`, and `/dev/null`. Porting to BSD is probably
doable; porting to Windows would be a different project.

---

## Configuration

Everything is stored under `$XDG_CONFIG_HOME/ea-music/`, or
`~/.config/ea-music/` if `XDG_CONFIG_HOME` isn’t set.

| File | Owner | Contents |
|---|---|---|
| `settings.conf` | `ytdlp.c` | `normalize=0|1`, `volume=0..200` |
| `library.conf` | `library_manager.c` | `repeat_all=0|1` |
| `*.lib` | You / the app | One song per line: `title|video_id|duration` |

The files are plain text. There’s no database or magic format. You can inspect
them with `cat`, search them with `grep`, edit them with `sed`, back them up
with `cp -r`, or sync them with Syncthing.

---

## License & forking

You’re free to use, fork, modify, redistribute, or sell this project. You can
even rename it to “EA-MUSIC ULTRA PRO MAX” and put it on a floppy disk.

I ask one thing: if you fork it, keep the Aoizen-fan disclaimer. It’s the whole
point. 🔥

I suggest adding an MIT `LICENSE` file if you want to make the licensing terms
formal.

---

## The human–AI tag team

This project came from a tough human and four very patient AIs.

I brought the idea, needs, taste, determination, and stubborn refusal to install
Electron. (Tough by me. From thought to need.)

Claude, ChatGPT, DeepSeek, and Gemini helped with architecture, code, refactors,
documentation, and the occasional existential crisis at 3 a.m.—in various
states of brilliance and confusion.

It was a tag team. It still is. I brought the need; the AIs helped bring the
product. We’re both still mad that no one else built this first. 😤

---

## Final words

> “To the best of my knowledge, there is currently no other project that does
> exactly this, the way this does it.”

I checked. Twice. I’m as surprised as you are.

So go listen to some music. Hit `Ctrl+2`, type something, and press `Enter`.
Save it with `Ctrl+A`. Play it with `Shift+Enter`. Toggle Repeat All with `R`
when you’re in the mood.

And if you’re an Aoizen fan—no hard feelings. Just… not for you. I’m polite
about it. Mostly. 🙏🔥