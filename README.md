<div align="center">

<img src="cia/banner.png" alt="VN3DS" width="384">

### A visual novel player for Nintendo 3DS that plays VNDS novels from the DS, PS Vita and Android ports, with no conversion needed.

[![Build](https://github.com/Pedro8b/VN3DS/actions/workflows/build.yml/badge.svg)](https://github.com/Pedro8b/VN3DS/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/Pedro8b/VN3DS?color=6d8cff)](https://github.com/Pedro8b/VN3DS/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Pedro8b/VN3DS/total?color=e2566f)](https://github.com/Pedro8b/VN3DS/releases)
![Platform](https://img.shields.io/badge/platform-Old%20%26%20New%203DS-lightgrey)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

**English** · [Русский](README.ru.md)

</div>

---

VN3DS is a rewrite of the [VNDS](https://github.com/BASLQC/vnds) idea for the 3DS. It is not a port of the old DS code.
Drop a novel folder (or a single `.zip`) onto the SD card and press **A**. VN3DS works out how the novel
is laid out, which formats its files are in and how big its pictures are, and you can start reading.

## ✨ Highlights

|  |  |
|---|---|
| 📂 **Opens any VNDS layout** | Classic DS/Android folders and zips, Vita ports, Higurashi‑Vita, a novel packed in one `.zip`, `.legArchive` files, or any mix of these, all detected automatically |
| 🔊 **Plays most audio formats** | Ogg Vorbis, MP3, AAC (ADTS, M4A/MP4, HE‑AAC), WavPack, WAV (PCM/ADPCM), FLAC. The format is detected from the file contents, not the extension |
| 🧩 **Finds files by name** | If the script asks for `music/s04.mp3` and the novel ships `sound/music/s04.wv`, VN3DS plays it. File names are also matched case‑insensitively |
| 🖼️ **Scales any image** | PNG, JPEG, BMP, GIF, TGA and PSD at any resolution. Images are scaled for the screen ahead of time with alpha‑aware filtering, and you can choose *Fit*, *Zoom* or *Stretch* |
| 🕶️ **Stereoscopic 3D** | The background sits behind the screen and the characters stand slightly in front of it. Move the 3D slider to turn it on |
| 📱 **Four screen layouts** | The classic DS split, the same thing swapped, or a standard VN look with a text box over the picture on either screen |
| 🌸 **Runs Higurashi natively** | Higurashi‑Vita `Scripts/*.txt` run on an embedded Lua 5.1, with chapter and TIPS navigation |
| 💾 **Keeps your progress** | 30 save slots plus a quick save and an autosave. A novel reopens where you stopped, and saves use the DS VNDS format |
| ⏩ **Skips line by line** | Holding R scrolls through the lines one at a time and speeds up the longer you hold. The typewriter effect, auto mode and text backlog are all there |
| 🧹 **Tidies converted scripts** | Ren'Py leftovers such as `\n`, `{i}` tags, quoted lines, `label …:` and speaker variables are cleaned up when the text is shown |

## 📥 Installation

1. Download the latest release from **[Releases](https://github.com/Pedro8b/VN3DS/releases/latest)**:
   - `VN3DS.cia`: install it with **FBI** (or another CIA manager) and it appears on the HOME Menu;
   - `VN3DS.3dsx`: copy it to `sdmc:/3ds/` and run it from the **Homebrew Launcher**.
   
   You can also get VN3DS from **Universal‑Updater** once it is listed there.
2. Sound needs a DSP firmware dump at `sdmc:/3ds/dspfirm.cdc`. If you have never made one, run
   [DSP1](https://github.com/zoogie/DSP1) once.
3. Put your novels on the SD card (see below) and start VN3DS.

Old 3DS / 2DS and New 3DS / 2DS XL are both supported. On a New 3DS, VN3DS turns on the 804 MHz mode and the L2 cache.

## 📚 Adding novels

Each novel goes in its own folder, or as a single `.zip`, inside any of these folders:

```
sdmc:/vnds/novels/          ← the usual place
sdmc:/3ds/vnds/novels/
sdmc:/3ds/VN3DS/novels/
sdmc:/3ds/save/novels/
sdmc:/VNs/
```

To scan other folders, add one `noveldir=sdmc:/path` line per folder to `sdmc:/3ds/VN3DS/config.ini`.

### Supported layouts

There is nothing to rename or repack. These are all recognised:

```
Classic VNDS (DS / Android)        Vita ports / VNDSVitaConverter      Higurashi-Vita (native)
───────────────────────────        ──────────────────────────────      ───────────────────────
MyNovel/                           MyNovel/                            Higurashi ep 1/
├── script/ or script.zip          ├── Scripts/                        ├── Scripts/*.txt  (Lua)
├── background/ or background.zip  ├── CG/  CGAlt/                     ├── CG/  CGAlt/
├── foreground/ or foreground.zip  ├── BGM/  SE/  voice/               ├── BGM/  SE/  voice/
├── sound/ or sound.zip            ├── *.legArchive                    └── includedPreset.txt
├── img.ini  info.txt              ├── isvnds
├── thumbnail.png  default.ttf     └── (or all of it in StreamingAssets/)
└── save/
```

- A zip may contain one top‑level folder, and inner zips are read in place when they are stored without compression.
- If there is no `main.scr`, the novel starts from the first `.scr`, which is how many Vita ports are made.
- Higurashi‑Vita chapters are read from `includedPreset.txt`, and the in‑game menu gets a **Chapters / TIPS** entry.

<details>
<summary><b>vn3ds.ini: describing an unusual layout by hand</b></summary>

If a novel is laid out in a way VN3DS doesn't recognise, put a `vn3ds.ini` in its root folder:

```ini
engine=vnds            ; or: higurashi
main=start.scr         ; first script (for higurashi: the script name without .txt)
script_dir=scripts2    ; extra folders, comma-separated; these are searched first
background_dir=bg_hd
foreground_dir=chara
sound_dir=bgm,se,voice
width=800              ; sprite coordinate space (same as img.ini)
height=600
title=My Novel
alt_art=1              ; Higurashi: use CGAlt sprites instead of CG
```

</details>

<details>
<summary><b>How files are looked up</b></summary>

1. The exact name in every matching folder and archive, ignoring case.
2. The same name with any supported extension (`bg/room.jpg` also matches `background/room.png`).
3. The name without its folder, and for sounds also under `voice/`, `se/`, `bgm/`, `music/` and `sound/`.

The decoder is then chosen from the file's first bytes, so a mislabelled `.ogg` that is really MP3 still plays.

</details>

## 🖥️ Screen layouts

<p align="center"><img src="docs/layouts.svg" alt="Screen layouts" width="100%"></p>

Change the layout under **Menu → Settings → Screen layout**. In the two *one screen* layouts the text appears in a
box over the picture, the other screen shows the backlog, and **B** hides the box so you can look at the art.

## 🎮 Controls

| Button | In the library | In a novel |
|---|---|---|
| **A** / tap | open novel | next line / pick a choice |
| **R** (hold) | | skip, line by line (speeds up the longer you hold) |
| **Y** | rescan SD card | auto mode on/off |
| **X** / **START** | exit (START) | menu |
| **SELECT** | | quick save |
| **B** | | hide/show the text box (one‑screen layouts) |
| **↑ / ↓**, Circle Pad, swipe | scroll | text backlog |
| **3D slider** | | stereoscopic depth |

The menu has Save, Load, Chapters/TIPS (Higurashi), Auto mode, Skip to next choice, Settings,
Restart from the beginning, and Exit to library.

## ⚙️ Settings

| Setting | Values |
|---|---|
| Text speed | very slow → instant |
| Music / sound volume | 0–100 % |
| Font size | 11–22 px (the novel's `default.ttf` is used; characters it lacks come from DejaVu Sans) |
| Auto mode delay | 1–9 |
| 3D depth | off / low / medium / high |
| Screen fit | **Fit** (whole picture, black bars) · **Zoom** (fills the screen, crops the edges) · **Stretch** |
| Screen layout | classic · swapped · one screen (top) · one screen (bottom) |
| Sprites stand on the bottom edge | fixes characters whose legs are cut off. **Each novel has its own value, and it is stored in its saves**: loading a save restores the value it was saved with |
| Autosave | every 2 minutes, before choices and on exit |
| Continue from the last save | opens a novel at its newest save |

## 💾 Saves and files

| What | Where |
|---|---|
| Saves (folder novels) | `<novel>/save/saveNN.sav`, in the VNDS XML format, so DS VNDS can load them |
| Saves (zip novels) | `sdmc:/3ds/VN3DS/saves/<novel>/` |
| Global variables | `global.sav` next to the saves |
| Settings | `sdmc:/3ds/VN3DS/config.ini` |
| Log, for bug reports | `sdmc:/3ds/VN3DS/log.txt` |

## 📜 Script support

- **VNDS `.scr`**: every VNDSx 1.4.9 command (`text` with its `~` `!` `@` prefixes, `bgload` with fades, `setimg`, `sound`, `music`,
  `choice`, `setvar`/`gsetvar`, `if`/`fi` (nested), `jump` to a file and label, `label`/`goto`, `delay`, `random`, `cleartext`, `endscript`),
  `$var` / `{$var}` substitution, and scripts in UTF‑8 (with or without a BOM) or UTF‑16. Unknown commands are skipped and written to the log.
- **Higurashi‑Vita**: `OutputLine`, `DrawScene`, `DrawBustshot`, `DrawSprite`, `FadeBustshot`, `DrawFilm`, `ShakeScreen`, `PlayBGM`/`PlaySE`/`PlayVoice`,
  flags, `CallScript`, and more, all running on real Lua 5.1 coroutines.
- **Scripts converted from Ren'Py** are cleaned up when shown: escape sequences, `{i}`/`{b}`/`{color}`/`{w}` tags,
  quotes around every line, `Name: "…"` speakers, and leaked `label` statements.

## 🔨 Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM, libctru, citro2d and these portlibs:

```sh
dkp-pacman -S 3ds-dev 3ds-libvorbisidec 3ds-libogg 3ds-zlib 3ds-liblua51
make                  # → VN3DS.3dsx
bash build.sh cia     # → VN3DS.3dsx + VN3DS.cia (needs makerom and bannertool on PATH)
```

On Windows, run these commands in the devkitPro MSYS2 shell. Every push is also built by
[GitHub Actions](.github/workflows/build.yml), and the `.3dsx` / `.cia` can be downloaded from the run's artifacts.

### Host tests

`tools/hosttest` builds the platform‑independent core (VFS, decoders, script engines, saves) for desktop Linux:

```sh
cd tools/hosttest && make
./hosttest scan  /path/to/novels               # what would the library show?
./hosttest run   /path/to/novel [steps]        # run the scripts headless and decode every resource
./hosttest audio file.wv out.wav               # decode one audio file
./hosttest image file.png 400 240 out.ppm      # decode and scale one image
```

### Source tour

```
source/core/   platform-independent: vfs (zip/legArchive/dirs), audio_decoder, image, script (VNDS),
               higurashi (Lua), save (VNDS XML), util
source/ctr/    3DS frontend: main loop, gfx (citro2d), font (stb_truetype atlas), audio (ndsp thread),
               library, game (scene / text / menus / 3D), config, ui
cia/           RSF, banner, CIA build script
```

## 🙏 Credits

- The original [**VNDS**](https://github.com/BASLQC/vnds), and the VNDS‑Vita and Higurashi‑Vita communities, for the formats VN3DS reads.
- Libraries: [stb_image / stb_truetype](https://github.com/nothings/stb), [dr_libs](https://github.com/mackron/dr_libs),
  [Tremor](https://gitlab.xiph.org/xiph/tremor), [FAAD2](https://github.com/knik0/faad2), [WavPack](https://github.com/dbry/WavPack),
  [Lua 5.1](https://www.lua.org/), [zlib](https://zlib.net/), [DejaVu fonts](https://dejavu-fonts.github.io/).
  See [THIRD_PARTY.md](THIRD_PARTY.md).
- Made by **[Pedro8b](https://github.com/Pedro8b)**.

## 📄 License

VN3DS is released under the **GNU GPL v3** ([LICENSE](LICENSE)), because it includes FAAD2, which is GPL. Third‑party components keep their own licenses.
VN3DS does not include any novels. Only play novels you are allowed to use.
