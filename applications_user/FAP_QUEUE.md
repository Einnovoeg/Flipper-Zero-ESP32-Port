# FAP queue (Momentum-Apps ports)

Checkers vendored from Next-Flip/Momentum-Apps `dev` for FAP builds via
`./buildFap.sh applications_user/<app>` (firmware must be built first).
CI builds it on the T-Embed job (see `.github/workflows/build.yml`,
"Build queued FAPs") and stages `faps/*.fap` into the board artifact.

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
