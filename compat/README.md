# Tanmatsu BSP display compatibility patch

`badge_bsp_display.c` is the Tanmatsu display implementation supplied with
`badgeteam/badge-bsp` 2.0.0, under its accompanying MIT license. The only change
returns the result of `st7701_initialize()` from `bsp_display_initialize_panel()`.
Upstream discards that result, allowing VCOM and callback initialization to run
after a failed display-ID probe or panel allocation/reset/initialization.

`bsp_display.cmake` replaces that one source in the resolved BSP target. The
component manager still resolves the published BSP and its dependencies; no
managed file or shared SDK file is edited. The original source SHA-256 is checked
at configure time so a future BSP update requires an explicit patch review.
`upstream.json` records the original and patched hashes. The host regression test
compiles this complete implementation with hardware calls substituted, checking
that panel errors stop initialization before settings and callbacks are touched.

Remove this source replacement once the selected upstream BSP propagates the
initialization error. The MIPI driver's display-ID probe order and panel-specific
initialization sequences are unchanged. Current-device hardware acceptance is
recorded in [validation.json](../validation.json); qualify additional display
variants separately when that hardware is available.
