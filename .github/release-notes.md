First public release of **VN3DS**, a visual novel player for Nintendo 3DS that plays VNDS novels from the DS, PS Vita and Android ports without converting them.

### Downloads
- **`VN3DS.cia`**: install with FBI; it appears on the HOME Menu.
- **`VN3DS.3dsx`**: copy to `sdmc:/3ds/` and run from the Homebrew Launcher.

Put novels in `sdmc:/vnds/novels/`. Sound needs `sdmc:/3ds/dspfirm.cdc` (run [DSP1](https://github.com/zoogie/DSP1) once).

### Features
- Classic VNDS, Vita and Higurashi‑Vita folders, a whole novel in one `.zip`, and `.legArchive` files are all detected automatically. `vn3ds.ini` covers anything unusual.
- Plays Ogg Vorbis, MP3, AAC/M4A (incl. HE‑AAC), WavPack, WAV and FLAC. Files are matched by name even when the extension in the script is wrong.
- Loads PNG, JPEG, BMP, GIF, TGA and PSD at any resolution, with Fit, Zoom or Stretch.
- Stereoscopic 3D, four screen layouts (including a standard ADV text box over the picture), and an option that stands sprites on the bottom edge of the picture. Each novel keeps its own value for that option, stored in its saves.
- Runs Higurashi‑Vita scripts on an embedded Lua 5.1, with chapters and TIPS.
- 30 save slots plus quick save and autosave, and a novel reopens at its last save. Saves can be loaded by DS VNDS.
- Skip goes line by line and speeds up the longer R is held. Auto mode and a text backlog are included, and Ren'Py‑converted scripts are tidied up when shown.

Runs on Old 3DS / 2DS and New 3DS / 2DS XL.

---
**RU:** первый публичный релиз. Описание на русском в [README.ru.md](https://github.com/Pedro8b/VN3DS/blob/main/README.ru.md).
