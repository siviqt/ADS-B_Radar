# Local ESP-IDF 6.0.2 LCD HAL override

Vendored unchanged from Nicolai-Electronics/tanmatsu-launcher commit
`20b1ddd31846a8d06de85d4ca1a7ca447c18e0bb`, `components/esp_hal_lcd/`:
https://github.com/Nicolai-Electronics/tanmatsu-launcher/commit/20b1ddd31846a8d06de85d4ca1a7ca447c18e0bb

The component matches ESP-IDF 6.0.2 except for three files: CMake adds
`esp_timer`, the MIPI header exposes the last-read timeout flag, and the MIPI
implementation bounds read-busy/read-empty waits to 50 ms. A timeout zeroes
the caller's buffer and sets the flag; a new read clears it. MIPI abstraction
2.0.0 uses this flag to detect a nonresponsive panel during display-ID and
VCOM OTP reads. The application explicitly enables
`CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD=y`; its default in the
upstream MIPI component is disabled. A CMake gate prevents disabling detection.
The existing write-FIFO waits are unchanged. The flag describes the most recent
read on this single DSI host; it is not a per-task synchronization mechanism.
ADS-B performs panel initialization before starting its display/UI workers.

Project components override IDF components without modifying the SDK installation.
`upstream-sha256.json` records the vendored file hashes, including the Apache-2.0
license copied from the installed ESP-IDF 6.0.2 tree. Remove this override only
after the selected IDF supplies the required API and equivalent bounded reads.
