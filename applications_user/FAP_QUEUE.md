# FAP queue (Momentum-Apps ports)

Batch 1 (21 apps) vendored from Next-Flip/Momentum-Apps `dev`, built via
`./buildFap.sh applications_user/<app>` (firmware must be built first).
CI builds them all on the T-Embed job (see `.github/workflows/build.yml`,
"Build queued FAPs", per-app isolated so one failure never hides others)
and stages `faps/*.fap|*.fal` into the board artifact — copy to SD `/ext/apps/`.

Games: checkers, chess, flappy_bird, minesweeper, solitaire, 2048, arkanoid,
t_rex_runner, reversi. Tools: passgen, metronome, qrcode, barcode_gen,
tone_gen, flashlight, fmf_to_sub, esubghz_chat, paint, text_viewer,
hex_viewer, programmer_calculator.

Porting notes (all verified against `firmware_api.c`, now 998 entries):
- `furi_assert`/`furi_check`/`ADD_SCENE`/`EXT_PATH`/`FURI_LOG_*` are macros —
  no API entries needed.
- Exported for this batch via `tools/add_symbol.py`: `dialog_ex_reset`,
  `canvas_draw_icon_animation`, `icon_animation_{alloc,free,start,stop}`,
  `furi_hal_speaker_{start,stop}`, plus earlier
  `subghz_block_generic_global_counter_override_get`.
- `buildFap.sh` now defines per-app `APP_DATA_PATH`/`APP_ASSETS_PATH`
  (mirrors fbt/uFBT) so upstream FAPs using them compile unmodified.
- This port's dialogs file browser is `dialog_file_browser_show` (newer SDK
  name); none of the batch-1 apps use the old `dialogs_file_browser_show`.

Display / color: the app canvas is 1-bit (`ColorWhite/Black/XOR` only); LCD
color comes from the driver's mono→RGB565 fg/bg theme mapping. Per-pixel
full-color games are NOT possible through the canvas API — that needs a new
color framebuffer API + driver support (firmware feature, proposed separately).
`esubghz_chat` needs live CC1101 verification on T-Embed after build.

## checkers — READY (expected to build)
- Upstream: `checkers` by H4W9 (FlipCheckers 1.0, Games).
- Needs only `gui` per its `application.fam`; 4 KB stack.
- API scan vs `components/flipper_application/flipper_application/firmware_api.c`:
  18 externals, 1 missing: `furi_assert` (likely macro-resolvable; else add via
  `tools/add_symbol.py` following the "missed api symbols" precedent).

## proto_pirate — ALREADY PRESENT (no action)
- Do NOT vendor Momentum-Apps `proto_pirate` 3.0 here: this repo already ships
  a port-adapted `applications_user/protopirate/` at **v3.2** (newer than
  upstream dev 3.0, with extra am/am_vag/fm/fm_f4 plugins and local fixes).
  Vendoring upstream alongside it breaks the firmware build with
  `Duplicate app declaration for 'proto_pirate'` in `tools/fam/generate.py`,
  so the upstream copy was removed again. If upstream ever pulls ahead,
  upgrade `protopirate/` in place instead of adding a second copy.
- API note (from scanning upstream 3.0): the only firmware gap it would have
  needed is `subghz_block_generic_global_counter_override_get` (now exported
  via `tools/add_symbol.py`, 990 entries); `sequence_error/success` were
  already exported, `furi_check`/`furi_assert` are macros, and the remaining
  `scene_previous`/helper refs are in-app.

## Building
```bash
./buildAndFlash_T-Embed.sh --build-only   # firmware first (ESP-IDF v5.4.1)
./buildFap.sh applications_user/checkers
./buildFap.sh applications_user/proto_pirate
```
CI builds these automatically: `.github/workflows/build.yml` has a "Build queued
FAPs (T-Embed only)" step (continue-on-error so experiments never block firmware
artifacts) that runs `buildFap.sh` for both apps and stages `faps/*.fap|*.fal`
into the `flipper-lilygo-t-embed-cc1101` artifact. Copy them to SD `/ext/apps/`.
