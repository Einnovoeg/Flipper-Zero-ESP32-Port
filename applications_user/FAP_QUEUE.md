# FAP queue (Momentum-Apps ports)

Sources vendored from Next-Flip/Momentum-Apps `dev` for FAP builds via
`./buildFap.sh applications_user/<app>` (firmware must be built first).
Not yet build-verified — see status per app.

## checkers — READY (expected to build)
- Upstream: `checkers` by H4W9 (FlipCheckers 1.0, Games).
- Needs only `gui` per its `application.fam`; 4 KB stack.
- API scan vs `components/flipper_application/flipper_application/firmware_api.c`:
  18 externals, 1 missing: `furi_assert` (likely macro-resolvable; else add via
  `tools/add_symbol.py` following the "missed api symbols" precedent).

## proto_pirate — NEEDS WORK (see below, then build)
- Upstream: `proto_pirate` 3.0 + embedded `protopirate_am_plugin` (.fal) by
  RocketGod-git, xMasterX, zero-mega et al. (Sub-GHz car-fob decoder).
- API scan of the *built* file set (main app + the 14 plugin sources listed in
  its `application.fam`) vs our firmware API table leaves these true gaps:
  - `furi_check`, `notification_error`, `notification_success`,
    `scene_previous` → add via `tools/add_symbol.py`.
  - `subghz_block_generic_global_counter_override_get` → verify it exists in
    `lib/subghz/blocks/generic.c`; if yes, export it.
  - Protocol symbols referenced only by plugin `.c` files *not* in the
    `application.fam` plugin sources (ford_v1+, honda_static, kia_v2+,
    land_rover, mazda_v0, mitsubishi_v0, fiat_marelli, porsche_cayenne,
    scher_khan…): either extend the plugin `sources=[...]` list to compile
    them in, or confirm they resolve from firmware (OFW has some natively;
    Momentum dev removed Starline/ScherKhan/Kia from its main app).
- NOTE: references to `fiat_marelli_*` / `porsche_cayenne_*` have no matching
  source file in the vendored tree — check upstream for renames before
  extending sources.
- Build gate: run `tools/check_fap_symbols.py` on the linked `.fap`/`.elf`
  (see `firmware_api.c` header) and iterate until clean, then test on T-Embed.

## Building
```bash
./buildAndFlash_T-Embed.sh --build-only   # firmware first (ESP-IDF v5.4.1)
./buildFap.sh applications_user/checkers
./buildFap.sh applications_user/proto_pirate
```
CI has no FAP job yet — add one that builds firmware then these two apps and
uploads the `.fap` artifacts (see `.github/workflows/build.yml`).
