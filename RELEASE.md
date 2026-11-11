# Rayman 2 The Great Escape (PS Vita) — Release v1.0

First public release of the native iOS **Mach-O loader** port of *Rayman 2: The Great Escape* (Ubisoft, iOS version by Gameloft) for the PlayStation Vita. The game is playable from the Gameloft logo and title screen through the menus, the in-engine intro and the first levels, at 60 fps with music and sound.

---

## 📦 Release Package Contents

1. **`rayman2thegreatescape.vpk`** (v01.00): PlayStation Vita application package with the Mach-O loader, vitaGL renderer, CoreAudio / OpenAL audio layer and physical input handling.
2. **`rebase.bin`**: binary relocation table (**8,505** absolute 32-bit pointers) for the iOS ARM executable `Rayman2` of *Rayman 2 The Great Escape* 1.0.1.

No game files are included: the music must be converted from your own copy with `tools/stage_data.sh` (see below).

---

## 🛠️ What is rebase.bin?

The iOS executable is a non-PIE image linked at `0x1000`; the loader maps it at `0x98000000`. `rebase.bin` lists every 32-bit word that holds an absolute address (data pointers, `__text` literal pools and ARM switch tables) so the loader can slide them at boot. It is generated from the executable with:

```bash
python3 -I tools/macho_prep.py game_app/Rayman2 rebase.bin
```

Layout (little-endian `uint32`): magic `RBS1`, word count, then the unslid VM address of each word to relocate.

---

## 🚀 Player Installation Guide

1. **PS Vita requirements**:
   - Custom Firmware (**Enso 3.60 / 3.65** recommended).
   - `kubridge.skprx` under `*KERNEL` in `ur0:tai/config.txt`.
   - `libshacccg.suprx` in `ur0:data/` (obtainable via **ShaRKBR33D**).

2. **Game assets**:
   - Extract `Payload/Rayman2.app/` from your decrypted iOS 1.0.1 `.ipa` (FairPlay `cryptid 0`).
   - On a PC, run `tools/stage_data.sh /path/to/Rayman2.app` from the repository (needs `python3`, `rsync`, `ffmpeg` with `libopus`): it converts the AAC `.BGM` music to Ogg Opus and writes `ux0_data/rayman2thegreatescape/`.
   - Copy that folder to `ux0:data/rayman2thegreatescape/`:
     ```text
     ux0:data/rayman2thegreatescape/
     ├── game_app/      (contents of Rayman2.app: Rayman2, DATA/, music converted)
     ├── rebase.bin
     ├── controls.txt   (created on first launch)
     ├── saves/         (created on first launch)
     └── logs/          (created on first launch, one log per run)
     ```

3. **Install the VPK**: transfer and install `rayman2thegreatescape.vpk` with VitaShell.

---

## 🎮 Controls

| PS Vita Input | Action |
| :--- | :--- |
| **Front Touchscreen** | Full touch UI (menus and on-screen buttons) |
| **Left Stick / D-Pad** | Move Rayman (virtual stick) |
| **Right Stick** | Camera |
| **Cross (✖)** | Jump (blue button) |
| **Square (◼)** | Shoot magic fist (red button) |
| **Circle (●)** | On-screen button 3 (right, above the red button) |
| **Triangle (▲)** | On-screen button 6 (top-left) |
| **L** | On-screen button 2 (left, look button) |
| **Start** | Pause |
| **Select** | Display mode: 816×544 (original aspect) ↔ 960×544 (stretched) |
| **L + R** | Show / hide the on-screen stick and red / blue buttons (hidden by default) |

---

## 📋 Changelog (v1.0)

- **2D graphics (logos, loading / title screens, menu text)**: the game pops the projection matrix stack once more than it pushes and pushes it twice; real OpenGL ignores both, vitaGL (1-entry stack, no checks) lost the 2D ortho matrix → the loader keeps its own GL-conformant matrix stacks.
- **2D graphics**: the frame blit pushed the projection / texture matrix every frame and overflowed vitaGL's stack → it now saves and restores them by value.
- **Hidden touch controls**: the on-screen stick and red / blue buttons are hidden by default; **L + R** toggles them (saved in `saves/loader_settings.txt`).
- **Language**: the game starts in English (the system-language mapping showed German for Spanish).
- **Audio**: the music and audio threads run above the game thread, so slow frames no longer cut the sound.
- **Logging**: the log writer thread was created with an invalid priority and never ran (logs stopped at the first frame) → fixed.
- Music: AAC `.BGM` files are played as Ogg Opus (converted by `tools/stage_data.sh`).
- "MORE GAMES!" (Gameloft online shop, UIKit screens) is disabled.

---

## ⚠️ Known Issues

- The game always starts in English; other languages can be chosen in the game's options.
- Hidden touch controls and saves have not yet been checked in a long play session on console.
- Online features ("MORE GAMES!") are disabled by design.

Installation and build instructions: see [README.md](README.md).
