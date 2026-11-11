# Rayman 2 The Great Escape — Port progress (iOS Mach-O)

The Phases section below is identical to the one in `PORTING_PLAN.md` (keep them in sync).

## Session 2026-11-04 (desk work to "console iteration only")

- **Runtime:** the init-time boilerplate (selector-name dispatch) was replaced by the objc_rt lineage
  (Zombie-Infection -> Block-Breaker-3-Unlimited: real ObjC class registration, MainWindow.nib emulation with
  `window`/`glView` outlets, NSTimer run loop, dyld-info-aware `macho.c`), because Rayman 2 has the same shell
  (`Rayman2AppDelegate` + nib-loaded `EAGLView initWithCoder:`, GLES1, NSTimer frame loop). From other donors:
  PoP-WW vitaGL softfp-libs fix + out-of-tree vitaGL build, Asphalt-4 boot checker, Skater-Nation hosttest.
  BB3-specific patches were dropped; `cxx.c` regenerated for Rayman 2 (`tools/gen_cxx.py`: 3 functions, 2 vtables).
- **Rayman-specific code:** `hle.h` identity; `darwin.c` Windows-path normalisation, Rayman imports (`clock` in
  Darwin units, `vfprintf`, `__stdoutp`, `rename`, `sleep`, `write`, mach host stats, ...); `objc.c` language
  mapping, CFArray/CFAbsoluteTime/CG/CC_MD5/reachability shims, version 1.0.1; `coreaudio.c` AudioFile (Ogg Opus)
  + AudioQueue + OpenAL entry points + CFURL; `gles.c` landscape blit (orientation 3, 816x544 / 960x544),
  `glFogx`/`glFrustumf`, frame counter; `uikit.c` orientation 3, `[BOOT] delegate ok`; `patch.c` controls from
  the game's HUD rects, `controls.txt`, `loader_settings.txt`, IGP shop disabled; `log.c` unbuffered until the
  first frame; `macho.c` logs `[UNIMPL]` at bind time.
- **Bugs found and fixed with hosttest (would have crashed on the console too):** `sprintf` INT_MAX size
  (truncated bundle path, no file opened); `sprintf` with overlapping `%s` (empty vignette name -> NULL buffer ->
  crash on NEW GAME); rebase false positive `0x2d3f4` used as register offset (crash in `GLI_vFogOff` on the first
  3D frame); "MORE GAMES!" opening an undrawable UIKit shop (black screen). Hosttest-only fixes: thread/semaphore
  UIDs must be positive, NSLock mutex size, read-write game data mount, `glFogx`.
- **Hosttest furthest point (2026-11-04, `hosttest/run.sh run 520` with scripted taps/pad):** boot -> Gameloft/Ubisoft
  logos -> title "touch the screen to continue" (music MondTH.BGM) -> 3D jungle main menu -> NEW GAME -> in-engine
  intro cinematic (Jail10.BGM, Jail20.BGM) -> first playable scene in the prison ship (HUD stick + buttons; ✕
  fired the magic fist, D-pad walked) -> still running at the 520 s timeout (`[GL] 3600 frames presented`, exit
  124 = timeout, no crash). Log excerpt:
  ```
  [INFO] rebase applied: 8505 words
  [INFO] imports: 629 slots, 0 unimplemented
  [INFO] [BOOT] delegate ok (Rayman2AppDelegate)
  [INFO] [GL] first frame presented
  [INFO] [AUDIO] BGM open ux0:data/rayman2thegreatescape/game_app/DATA/WORLD/SOUNDIPHONE/MONDTH.BGM: 48000 Hz, 2 ch, 1275055 frames
  [INFO] [AUDIO] AudioQueue thread up: port 0, source 48000 Hz 2 ch
  [INFO] NSLog: get msg play Jail10.BGM loop = FALSE
  [INFO] [GL] 3600 frames presented
  ```
- **Not done / out of scope:** no deploy, no commit (as requested); Ghidra pseudo-C not generated; GXM/vitaGL
  rendering, real performance and real audio output can only be judged on the console (`[~]` items).

## Phases

**Legend.** `[x]` = done and verified at the desk (build, hosttest or script output; the evidence is cited
on the item). `[~]` = implemented, pending verification on the real Vita (the item says exactly which log
line or behaviour to check). No item is left `[ ]`. Desk evidence comes from: `psvita-toolkit build
--preset release` (VPK `build/rayman2thegreatescape.vpk`), `hosttest/run.sh` (loader in Docker Linux ARM32
softfp, log `build/hosttest-data/rayman2thegreatescape/logs/rayman2thegreatescape_001.log`, screenshots
`build/hosttest-data/frames/*.ppm`), and the tools in `tools/`.

### Phase 1: Repo and tools
- [x] IPA extracted, `game_app` symlink, loader merged, CMake/`hle.h` adapted to `rayman2thegreatescape` (2026-11-04: runtime replaced by the Block-Breaker-3 / Zombie-Infection objc_rt lineage, see session entry).
- [x] Executable decrypted (cryptid 0) -- `tools/macho_prep.py` prints `FairPlay cryptid: 0 (descifrado)`.
- [x] `rebase.bin` regenerated with the newer `tools/macho_prep.py`: **8505 words** (`docs/macho_report.txt`), regression `python3 -I tools/test_macho_prep.py` -> `macho_prep ok (14 casos, 8505 palabras)`.
- [x] `.gitignore`: `Makefile` anchored to `/Makefile` (it excluded `vendor/vitaGL/Makefile` from the toolkit's source copy: first build failed with `No targets specified and no makefile found`).

### Phase 2: Decompilation and inventory
- [x] `decompiled/macho/` dumps (frameworks, ObjC classes, symbols, imports) -- section 2.
- [x] Disassembly for triage without Ghidra: `tools/annotate.py` extended for non-PIC ARM literals and `ldr rX,[pc,rY]` PIC loads (resolves selectors/classes/strings/globals); used for every address in section 1b. Ghidra pseudo-C was not generated (not needed so far; `mcp__ghidra`/REA available if a crash needs it).
- [x] Imports classified: `docs/imports.md` (generated by `python3 -I tools/imports_table.py`): 322 imports = **218 real, 39 stub, 57 host-class, 8 const, 0 UNIMPL**. Stubs = network (sockets, reachability, NSURLConnection), Gameloft IGP shop UI (CG gradients/masks, CC_MD5), C++ exception unwinding (SjLj).
- [x] Table symbol -> shim file -> status written (`docs/imports.md`). Hosttest log confirms `imports: 629 slots, 0 unimplemented`.

### Phase 3: Real engine analysis
- [x] Engine identified by symbols: Ubisoft CPA/OpenSpace + Gameloft iOS shell (section 1b).
- [x] Lifecycle mapped: MainWindow.nib -> `applicationDidFinishLaunching:` -> NSTimer `DisplayText` -> `_MainGameLoop`; present via `_endScene` -> `presentRenderbuffer:` (section 1b).
- [x] Resource opening mapped: `fopenFullPath` (`_g_bundleDir` + upper-cased name, `\` -> `/`), saves `\..\Documents\RAYMAN_2_*` (section 1b). Hosttest: `fopen(.../game_app//../Documents/RAYMAN_2_SY_ -> .../saves/RAYMAN_2_SY_)`.
- [x] Middleware: none; OpenAL (SFX), AudioFile+AudioQueue (BGM), IGP/analytics = stub, no GameKit/StoreKit (section 1b).
- [x] Memory budget checked: `__DATA` 4 MB, iPhone-3G-era game, 256 MB newlib heap; data streamed per level (section 1b).

### Phase 4: Loader and ObjC bootstrap (GATE: log reaches the app delegate)
- [x] `macho_load()` maps `__TEXT`/`__DATA` at `0x98000000`, applies `rebase.bin`, binds imports -- hosttest: `Mach-O: TEXT 0x00001000+0xD9000 DATA 0x000DA000+0x3D3000 -> real 0x98001000`, `rebase applied: 8505 words`.
- [x] Every import resolves to a shim; unresolved ones would log `[UNIMPL] code stub bound: <name>` at bind time and `[UNIMPL] called: <name>` when called (macho.c) -- hosttest: `imports: 629 slots, 0 unimplemented`.
- [x] `macho_run_inits()`: the binary has no `__mod_init_func` -- hosttest `running 0 global constructors... done`.
- [x] ObjC runtime: guest classes from `__objc_classlist`, host classes on demand, NSString/NSArray/NSDictionary/NSUserDefaults/NSBundle (`objc.c`, `objc_rt.h`, `uikit.c`) -- hosttest reaches the delegate (next item).
- [x] Delegate called -- hosttest: `-> applicationDidFinishLaunching:` ... `[BOOT] delegate ok (Rayman2AppDelegate)`, `run loop started`.
- [~] Same on the Vita: check `[BOOT] delegate ok (Rayman2AppDelegate)` in `ux0:data/rayman2thegreatescape/logs/rayman2thegreatescape_001.log`.

### Phase 5: Graphics (GATE: first frame on screen)
- [x] vitaGL softfp build: vendored vitaGL built out-of-tree with `SOFTFP_ABI=1 SOFTFP_LIBS=1` (vitasdk-softfp toolchain detected: `-- vitasdk-softfp toolchain: softfp libs`), PoP-WW `math_utils.h` fix (no `pcs("aapcs-vfp")` on softfp libmathneon); `._*` excluded by rsync before `make`.
- [x] EAGLContext / CAEAGLLayer / `renderbufferStorage:fromDrawable:` / OES FBO shims -- hosttest: `EAGLContext initWithAPI:1`, `EAGL drawable renderbuffer = 1 (320x480)`, `EAGL window framebuffer = 2`.
- [x] Orientation/aspect: 320x480 portrait FBO rotated to landscape (orientation 3) at 816x544 centered (default) or 960x544 stretched (SELECT) -- hosttest screenshot `frames/0005.ppm` = title screen upright with side bars; `frames/0009.ppm` = 3D main menu.
- [x] `presentRenderbuffer:` -> `vglSwapBuffers`, game driven by its own NSTimer (`NSTimer: every 0.0083s -> -[Rayman2AppDelegate DisplayText] (repeats)`) -- hosttest `[GL] first frame presented`, `[GL] 4800 frames presented` after ~6 min under qemu.
- [x] Textures: engine-decoded `.GF` -> `glTexImage2D` (no CgBI/PVRTC uploads in this game; `glCompressedTexImage2D` is not imported) -- hosttest renders textured title/menu.
- [x] GLES2/GLSL: not used (GLES1 fixed function only, `initWithAPI:1`).
- [~] First frame on the Vita: check `[GL] first frame presented`, then the Gameloft/Ubisoft logos and the title screen upright, not mirrored. If mirrored/upside down: swap `uv[]` in `gles_present()` (and keep `input.c` in sync).
- [~] Fog/alpha-test look on GXM (vitaGL FFP) vs Mesa: compare the jungle menu (`frames/0009.ppm`) with the console.

### Phase 6: Touch
- [x] Front touch -> `UITouch` in portrait view coordinates (inverse of the landscape blit in `input.c`), consistent with the game's `_UpdateTouch` (x_game = view.y, y_game = 320 - view.x). Hosttest: scripted taps (`HOSTTEST_TAPS`) passed "touch the screen to continue" and opened the main menu.
- [x] `touchesBegan/Moved/Ended:withEvent:` delivered to EAGLView, multitouch (uikit.c `run_touches`).
- [x] Rear panel: disabled (not sampled).
- [~] On the Vita: tap NEW GAME / OPTIONS on the menu and the on-screen stick/buttons in game; they must react where they are drawn.

### Phase 7: Physical controls
- [x] Mapping via synthetic touches on the game's own HUD rects read at runtime (`patch.c`), layout logged -- hosttest: `[INPUT] DpadButton x 10..120 y 200..310`, `tp_button[0] x 408..478 y 183..253`, `[1] x 337..407 y 240..310`, `[2] x 0..32 y 70..102`, `[3] x 408..478 y 110..180`, `[4] x 0..32 y 0..32`, `[5] whole screen`, `[6] x 19..67 y 32..70`. Left stick/D-pad -> virtual stick drag, right stick -> camera drag, buttons -> held touch at button centre.
- [x] Dead zone 12 / stick radius 40 defaults, configurable.
- [x] `controls.txt` in `ux0:data/rayman2thegreatescape/` (`CROSS = BTN0` ...); missing file = defaults and a commented template is written -- hosttest: `[INPUT] controls.txt missing: defaults (template written)`.
- [x] Hosttest in the first level (Jail, after the intro): a scripted ✕ press mapped to BTN0 lit the red button and Rayman threw a magic fist; D-pad Up walked Rayman forward through the virtual stick (screenshots `frames/0013..0025.ppm`). Defaults set accordingly and re-verified (final hosttest run, `frames/0019.ppm`: ✕ lights the blue BTN1 and Rayman jumps; `frames/0021.ppm`: □ lights the red BTN0 and he throws a fist): CROSS = BTN1 jump, SQUARE = BTN0 shoot.
- [~] Still to confirm on the Vita: the same ✕/□ behaviour, CIRCLE/TRIANGLE/L/START targets (BTN3, BTN6, BTN2, BTN4 = top-left pause icon), analog stick strength (STICK_RADIUS) and right-stick camera; fix `controls.txt` / defaults in `patch.c` if needed.
- [~] Menus with buttons: menus are touch-driven on iOS; D-pad/✕ navigation of `_menu_item` rects is not implemented (front touch works).

### Phase 8: Audio
- [x] What the binary uses (evidence section 1b): OpenAL for SFX (vitasdk openal-soft, bound directly), AudioFile + AudioQueue for music (`coreaudio.c`: AudioFile over libopusfile, AudioQueue = own thread on a sceAudioOut MAIN port 48 kHz; OpenAL keeps the BGM port), AudioSession = ok/0 (`IsOtherAudioPlaying` reads 0).
- [x] Music formats inventoried: 158 `.BGM` = AAC m4af 22050 Hz stereo (91 MB); `.BNM` SFX banks decoded by the engine itself; `.APM` unused. No on-device AAC decoder -> `tools/stage_data.sh` re-encodes `.BGM` in place to Ogg Opus 48 kHz stereo 96 kbps (ffmpeg libopus): `converted 158 ... failed 0`, `158 BGM in Ogg Opus` (113 MB), ffprobe `codec_name=opus sample_rate=48000 channels=2`.
- [x] BGM playback path verified in hosttest: `NSLog: get msg play MondTH.BGM loop = TRUE`, `[AUDIO] BGM open .../MONDTH.BGM: 48000 Hz, 2 ch, 1275055 frames`, `[AUDIO] AudioQueue thread up: port 0, source 48000 Hz 2 ch`, fake sceAudioOut `audio port 0: 11059200 frames @48000 Hz, peak 13907` (non-silent PCM, 230 s continuous).
- [x] Deadlock rule handled: `AudioQueueDispose/FreeBuffer` never wait for the audio thread (CBGMPlayer::Clean holds `g_queueMutex`, which the callback takes); the thread frees the queue. Hosttest survived `get msg stop` / level change.
- [x] SFX path: OpenAL context and sources created by the game in hosttest (openal-soft null backend; `alGenSources`/`alSourcePlay` bound).
- [~] On the Vita: music on the title screen (`[AUDIO] AudioQueue thread up: port N`), SFX in menus/game (OpenAL), no crackle (MAIN port grain 1024 @48 kHz), volume sliders in OPTIONS change BGM (`AudioQueueSetParameter` -> `sceAudioOutSetVolume`) and SFX; music pauses/resumes across PS-button suspend.

### Phase 9: Videos / FMV
- [x] Inventory: **0 video files** in the IPA (no `.m4v/.mp4/.mov/.avi/.bik`; `find game_app -iname ...` empty), no `MPMoviePlayerController` import, CPA `fn_p_stVideo` (0x40a34) is a stub in this build. All story scenes are in-engine cinematics rendered by GL with BGM (`CineGlob.BGM`, `CineLyTH.BGM`), covered by Phases 5/8.
- [x] Graceful skip kept anyway: `uikit.c` MPMoviePlayerController host class posts `MPMoviePlayerContentPreloadDidFinishNotification` / `PlaybackDidFinishNotification` at once (never a black hang) -- unused by this binary.
- [x] Hosttest: NEW GAME plays the in-engine intro (prison ship, "SLAVES NOW ON BOARD", silver lum, SKIP button, subtitles) with its music (`get msg play Jail10.BGM`, `[AUDIO] BGM open .../JAIL10.BGM`), then hands control to the player (HUD stick/buttons appear) -- `frames/0006..0013.ppm`.
- [~] On the Vita: same sequence; SKIP (touch) works; no stall between cinematic and gameplay.

### Phase 10: Assets, data, language and saves
- [x] Paths: `NSHomeDirectory`/`Documents` -> `saves/`, `Library/Caches` created, bundle -> `game_app/`; `darwin_path()` converts `\` -> `/` and collapses `//` and `/../` -- `sh tools/test_darwin_path.sh` -> `darwin_path ok (8 cases)`; hosttest `... -> ux0:data/rayman2thegreatescape/saves/RAYMAN_2_SY_`.
- [x] Boot checker: `main.c boot_check()` shows a system message dialog with the exact path if `game_app/Rayman2`, `rebase.bin` or `game_app/DATA/GAME.DSB` is missing (hosttest fake prints the dialog text; code path built into the VPK).
- [x] Language: Vita system language -> `AppleLanguages` (`en fr de it es ja`, others -> `en`) seeded in `NSUserDefaults` and `NSLocale preferredLanguages` -- hosttest `[LANG] AppleLanguages = (en)` and the game shows English.
- [x] Saves written: hosttest `fopen ok .../saves/RAYMAN_2_` (options file `RAYMAN_2_SY_` + game slots).
- [~] On the Vita: set the system language to French/Spanish/... and check menus change; save after the first level, close and relaunch, `CONTINUE` must load it; files appear in `ux0:data/rayman2thegreatescape/saves/`.

### Phase 11: Performance
- [x] 444/222/222/166 clocks at boot (`main.c`).
- [x] Logging made cheap after boot: every line is written immediately until the first frame (a boot crash keeps the full log), then INFO lines are buffered (`log_set_buffered`); `[GL] N frames presented` every 600 frames.
- [~] Frame rate on the Vita: time between `[GL] 600 frames presented` lines (60 fps target = 10 s per 600 frames; the game timer asks for 120 Hz). Use `psvita-toolkit perf-telemetry` / `mem-profile` for a 30-minute session; watch for growth (retain/release are no-ops in objc.c).

### Phase 12: Configurator and options
- [x] Persistent loader settings in `saves/loader_settings.txt` (`DISPLAY = 0|1`, toggled with SELECT), controls/dead zone/stick radius in `controls.txt` (Phase 7). The game's own options (volumes, etc.) are saved by the game in `saves/RAYMAN_2_SY_`.
- [~] On the Vita: SELECT switches 816x544 <-> 960x544 and the choice survives a restart.

### Phase 13: Trophies
- [x] GameKit/StoreKit are not linked (`frameworks.txt`): no achievements to map; no trophy pack is shipped (not required for a first console run).

### Phase 14: LiveArea and VPK
- [x] LiveArea from the IPA's `iTunesArtwork` (icon0 128x128, bg0 840x500, pic0 960x544, startup 280x158, 8-bit indexed) -- `psvita-toolkit livearea --validate`: 5/5 OK.
- [x] `param.sfo` Title ID `PSVRTGE02`, VPK built: `[+] Build OK. VPK: build/rayman2thegreatescape.vpk` (no compiler warnings in `source/`).
- [~] Install the VPK with VitaShell: no warnings, LiveArea shows the artwork.

### Phase 15: Real-hardware testing and release
- [~] Full playthrough without crashes; suspend/resume OK; Vita 1000/2000/PSTV status stated (console run #1 checklist below).
- [~] Production build: `PORT_TRACE` / extra logging off once stable (`TMPDBG`-style traces are not left in the code).
- [~] README: state IPA v1.0.1 (bundle `com.gameloft.Rayman2`, CFBundleVersion 1.0.1) and that a decrypted IPA is required.

### Extra items done this session
- [x] Hosttest (`hosttest/`, adapted from Skater-Nation): `hosttest/run.sh build` / `run [s]`, reuses the Skater Nation Docker images + `libopusfile`; scripted input `HOSTTEST_TAPS` / `HOSTTEST_PAD`; crash handler prints the guest caller through `ml_align_call` and flushes the buffered log. Furthest point: see the session entry.
- [x] Fixed `[sprintf]` truncation: `d_sprintf` passed `INT_MAX` as size, glibc computed `buf + size` past 0xFFFFFFFF for guest buffers and cut the output (`_g_bundleDir` `game_app/` -> `game_ap`: no data file opened). Now bounded to 1 MB.
- [x] Fixed `[sprintf]` in-place formatting: `sprintf(buf, "%s%02d.gf", buf, n)` (VIG_fn_vLoadBMPVignette) gave `01.gf` -> NULL vignette buffer -> crash in `GLD_vYInvertBitmap24b` when starting NEW GAME. `darwin_printf.c` copies an overlapping `%s` source first.
- [x] Fixed rebase false positives: struct offset `0x2d3f4` (equal to the start of `_AGO_vAddParticleContd`) used as `str r2, [r3, r1]` index was relocated -> crash in `GLI_vFogOff` (0x8c330) on the first menu frame. `macho_prep.py` now skips literals used as the index register of a load/store; 8 words removed (all `0x2d3f4`), 2 regression cases added.
- [x] "MORE GAMES!" (Gameloft online shop, UIKit table views we cannot draw: portrait switch + black screen in hosttest) disabled: `fn_p_stLaunchIGP` store at 0x4ee10 patched to `nop` (`patch_word` checks the original word).
- [x] `tools/stage_data.sh` rewritten for this game (BGM conversion, incremental, AppleDouble cleanup, self-checks); `tools/deploy_data.py --logs` fetches `rayman2thegreatescape_NNN.log`.

## Console run #1 checklist

1. PC: `tools/stage_data.sh` (prints `ux0_data/rayman2thegreatescape ready (778M) ... 158 BGM in Ogg Opus`), `psvita-toolkit build --preset release`.
2. Vita (VitaShell FTP on): `psvita-toolkit deploy --data` (or `python3 -I tools/deploy_data.py`) uploads `ux0:data/rayman2thegreatescape/` (`game_app/`, `rebase.bin`, `saves/`, `logs/`), then install `build/rayman2thegreatescape.vpk` (`psvita-toolkit deploy`). `libshacccg.suprx` must exist in `ur0:data/`.
3. Launch, play to the first controllable scene, quit; `python3 -I tools/deploy_data.py --logs` -> `logs/rayman2thegreatescape_NNN.log` (+ any `.psp2dmp` from `ux0:data/`).
4. Lines that prove each subsystem (in order):
   - Loader: `rebase applied: 8505 words`, `imports: 629 slots, 0 unimplemented`.
   - Boot: `-> applicationDidFinishLaunching:`, `[LANG] AppleLanguages = (xx)`, `[BOOT] delegate ok (Rayman2AppDelegate)`, `run loop started`.
   - GL: `EAGL drawable renderbuffer = 1 (320x480)`, `[GL] first frame presented`, `[GL] 600 frames presented`.
   - Input: `[INPUT] DpadButton x 10..120 y 200..310` + 7 `tp_button` lines.
   - Audio: `[AUDIO] BGM open .../MONDTH.BGM: 48000 Hz, 2 ch`, `[AUDIO] AudioQueue thread up: port N`.
   - Video: none expected (no FMV in this IPA; cinematics are in-engine).
5. Likely failures and what to do:
   - Message dialog "Missing ..." -> data not deployed to the exact path shown; rerun step 2.
   - `FATAL Missing libshacccg.suprx` -> install it with ShaRKBR33D.
   - Crash before `[BOOT]` -> `psvita-toolkit analyze <dump> --elf build/rayman2thegreatescape`; PC in 0x98xxxxxx: subtract 0x98000000, `tools/annotate.py game_app/Rayman2 <function>`; a register holding a small unslid address = rebase miss (add case to `tools/test_macho_prep.py`), a register = 0x98xxxxxx where a number was expected = rebase false positive.
   - `[AUDIO] AudioQueue: sceAudioOutOpenPort(MAIN) = 0x...` -> no free MAIN port: lower OpenAL's or reuse one port.
   - `[AUDIO] AudioFileOpenURL(...): not Ogg Opus` -> `stage_data.sh` not run on this copy.
   - Black screen with `[GL] N frames presented` growing -> vitaGL state issue: compare with `frames/*.ppm` from hosttest; try `DISPLAY = 1`.
   - Image upside-down/mirrored or touches offset -> `uv[]` in `gles_present()` + `input.c` mapping (keep both consistent).
   - `[OBJC] unhandled -[X sel]` lines right before a crash -> add the host method in `objc.c`/`uikit.c`.
   - Slow (< 30 fps) -> check `[GL]` interval; `psvita-toolkit perf-telemetry`.

## Console run #1 result (2026-11-10)
- Reached the 3D main menu. Boot vignette = white quad on black; menu 2D text invisible; sound stutters on frame drops. Hosttest (Mesa) draws all of it, so the 2D issue is vitaGL-side.
- Log stopped at `[GL] first frame presented`: the log drain thread was created at `0x10000100 + 60` (= 220, outside 64..191) and never ran. Fixed to `+ 30`; hosttest fake now rejects out-of-range priorities.
- Audio threads (`coreaudio`, `bgm_queue`) moved to `0x10000100 - 20`, above the game thread.
- Engine 2D path found: `fn_OGLThread_vUnlockDevice` / `fn_OGLThread_vWrite16bBitmapToBackBuffer` (0xcfac4 / 0xcfcd4) convert a 565 software buffer to RGB888, `glTexImage2D(GL_RGB, GL_UNSIGNED_BYTE)`, draw one quad (`fn_OGL_vDisplayTexture*`, blend/depth off) and delete the texture in the same frame. New `[GL] texture WxH ... ifmt/fmt/type` lines (one per distinct shape) give the evidence for run #2.

## Console run #2 result (2026-11-10)
- Log drain fix confirmed: the log now runs past the first frame (BGM `MONDTH.BGM` open, `AudioQueue thread up: port 7`, `[GL] 600 frames presented`).
- `[GL] texture` lines: every RGBA8 upload (3D) renders; the only non-RGBA ones are `512x512 GL_RGB` (boot vignette, tex 2 = white quad) and `1024x512 GL_LUMINANCE_ALPHA` (font = invisible 2D text). `g_glTexImage2D` now expands both to RGBA8 on the CPU (honours the default 4-byte row alignment). Not yet verified on the console; no Docker on this host for a hosttest run.

## Console run #3 result (2026-11-10)
- 60 fps in game (`[GL] N frames presented` every 10.0 s); BGM changes per level OK (MondTH -> Jail10 -> Jail20).
- RGBA expansion of the GL_RGB/LA uploads did NOT fix the white boot quad (= Gameloft logo, which has a white background) nor the missing loading/title screens: the upload format is not the cause. Kept for now (one change per run); remove if run #4 shows it is unrelated.
- Language: AppleLanguages `es` (index 4 in `_deviceLanguageList`) showed German. Index -> language goes through the engine byte `g_stEngineStructure+0xc28` (set by `fn_p_stStringProc` script op 0x4e1e8 and `fn_bLoadGameOptions`), not traced. Loader now always sends `en`.
- Next evidence: `[2D] quad #N` lines (one per distinct 2D quad: bound texture, caps, env mode, color, xy/uv, MV/PR/TX matrices) in `g_glDrawArrays`.

## Console run #4 result (2026-11-10)
- English OK; cutscene improved. Still no 2D at all (menu screenshot `screenshots/db/2026-11-10/2026-11-10-184049.jpg`: 3D jungle only). No `[2D]` lines: the engine's 2D quads are `GL_TRIANGLE_FAN` (`fn_OGL_vDisplayTextureWithCoord` 0xcf8f4, `GLI_fn_vDisplayJPFont` 0x8c8f0), the filter only took strips.
- Root cause found statically: both 2D paths call `ChangeProjectionToOrthoMatrix` (0xd122c), which `glPopMatrix`es the PROJECTION stack to get the ortho matrix the game keeps pushed (`g_ProjectionMatrixLevel`). vitaGL's projection/texture stacks hold a single push (`GENERIC_STACK_DEPTH 2`, counter starts at 1) and `NO_DEBUG` skips the overflow check, so `gles_present()`'s own push/pop of PROJECTION and TEXTURE every frame overwrote the game's ortho matrix. `gles_present()` now saves/restores those two matrices by value (`glGetFloatv` + `glLoadMatrixf`). Mesa (hosttest) has deeper stacks, which is why the desk never showed it.
- RGBA expansion of GL_RGB/LA uploads removed (unrelated). `[2D]` dump now also takes fans (xyz font quads).

## Console run #5 result (2026-11-10)
- `[2D] quad #1` (Gameloft logo, tex 2): xy in landscape pixels (0..479 x 0..319), uv 0..0.9375 x 0..0.625, but PROJECTION = identity -> only the logo's top-left corner (white background) filled a screen quadrant = the "white square".
- Cause: `fn_OGLThread_vInitOglFlags` (0xcf0ac) sets the ortho+rotation as the base PROJECTION matrix; `ChangeProjectionToOrthoMatrix` pops at depth 0 on its first call and `SetFrustumMatrix` pushes on top of `ChangeProjectionToFrustumMatrix`'s push. GL ignores the underflow/overflow; vitaGL (1-entry projection/texture stack, NO_DEBUG = no checks) loaded identity and lost the ortho for good.
- Fix: guest `glPushMatrix`/`glPopMatrix` now use loader-side stacks (32 deep per modelview/projection/texture unit, underflow/overflow ignored as in GL), via `glGetFloatv` + `glLoadMatrixf`. Not yet verified on the console.

## Console run #6 result (2026-11-10)
- All 2D confirmed on console after the guest matrix stacks: Gameloft logo, loading/title screens, menu text, in-game HUD (`screenshots/dh/2026-11-10/2026-11-10-190928.jpg`). 60 fps.
- Touch HUD hiding: `patch_hide_quad()` (patch.c) skips 4-vertex client float fans/strips fully inside the stick / BTN0 / BTN1 rects (+12) while `_g_ProjectionMatrixLevel == 2` (ortho) and the HUD is enabled (`+0x1c` != 0). Hidden by default, L+R toggles, saved as `HUD = n` in `saves/loader_settings.txt`. Not yet verified on console.
- Debug dumps removed (`[2D] quad`, `[GL] texture`). README / RELEASE.md written for v1.0.
