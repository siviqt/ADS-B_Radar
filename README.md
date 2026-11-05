# ADS-B Radar for Tanmatsu

**Accepted release: 0.3.2 / revision 11, BSP major 2.** The user confirmed hardware
tests passed on 1 November 2026. [validation.json](validation.json) records the
current release and acceptance summary.

This repository contains the current native ESP-IDF application for Tanmatsu.
It provides Map, Radar, Arrivals and Stats screens,
aircraft details, filters, saved settings, worldwide offline SD maps and English
city search. Fresh installations default to Sofia; saved locations and providers
take precedence. F1/Esc goes back or opens the exit choice, F2/F3 changes views,
F4 opens filters, F5 opens settings, and F6 opens the menu with SD retry and Wi-Fi
reconnect. Find city is available in settings.

## Source layout

- `main/`: application, display integration, data services and LVGL interface.
- `components/` and `compat/`: required local HAL and BSP compatibility sources.
- `assets/world/`: current worldwide SD atlas and city index, with notices.
- `assets/icons/`: launcher icons and original artwork.
- `tools/` and `tests/`: asset generation, package validation and host checks.
- `dependencies.lock` and `sdkconfig.defaults`: pinned dependencies and build defaults.

The original PlatformIO board targets and their configuration are outside this
Tanmatsu source tree. Wi-Fi profiles and saved locations are read from the device;
no private network credentials or device settings are bundled here.

## Build

Activate **ESP-IDF 6.0.2**, then run these commands from this directory:

```sh
./build.sh
```

The component manager restores `managed_components/` using the pinned
`main/idf_component.yml` and `dependencies.lock`. `sdkconfig` is generated from
`sdkconfig.defaults`; both it and `build/` are disposable local build files.
The firmware is `build/adsb_bringup.bin`. The build enforces the 2 MiB image limit
and writes its size, allocation and SHA-256 to `build/adsb_bringup.appfs.json`.

| Dependency | Version |
|---|---|
| ESP-IDF | 6.0.2 |
| Badge BSP | 2.0.0 |
| MIPI DSI abstraction | 2.0.0 |
| LVGL | 9.2.2 |

Install the application binary through the launcher/AppFS. The partition table
is used for build-time checks; application updates do not require flashing the
bootloader, partition table, launcher, radio or coprocessor firmware.

## Hardware integration

The BSP owns hardware initialization, buses and SD mount/unmount operations.
The app owns LVGL, input consumption, atlas readers/cache and retry policy.
Readers are closed before a remount and during bounded shutdown to the launcher.

The display uses RGB565, logical 800×480 landscape, partial 24-line buffers and
clockwise rotation onto the physical 480×800 panel. The app owns the completion
callback and waits up to one second before returning to the launcher on an
unknown transfer state. It does not use the BSP's generic wait stub.

MIPI 2.0.0 selects the second display sequence for panel ID `42 00 01` and keeps
the original sequence for the other variant. Startup logs the detected ID and
variant. The app explicitly enables bounded DSI-read detection and retains the
[local LCD HAL](components/esp_hal_lcd/README.adsb.md). The
[BSP compatibility patch](compat/README.md) propagates panel initialization errors.
Their source provenance and licenses are kept alongside the code.

Pre-revision-3 P4 support, software ECDSA verification, TLS certificate/hostname
checks, RTC retention and the NVS-only flash-write guard are retained. Existing
launcher Wi-Fi profiles and LCD calibration are read without a settings migration.

## Assets

- `assets/world/`: the current atlas, city index and required data/license notices.
- `assets/source/`: optional, ignored input cache used to rebuild those assets.
- `assets/icons/`: the current launcher icon sizes, original artwork and prompt.
- `tools/atlas-source-lock.json`: source URLs and hashes.

Maps and city search use SD assets and work offline. F6 → Retry SD assets handles
a missing or changed card. Retain `DATA_NOTICES.txt` with distributed atlas data.

The current SD assets are retained because they are required at runtime. Large
input caches, generated firmware, packages and build logs are excluded. To fetch
or verify input snapshots and rebuild the atlas using a Python environment with
the pinned host dependencies:

```sh
python tools/fetch_atlas_sources.py --output assets/source
python tools/build_atlas.py --source assets/source --output assets/world
```

GeoNames publishes rolling dumps. If a download no longer matches the pinned
hash, supply the matching archived snapshot or explicitly update
`tools/atlas-source-lock.json` before rebuilding. The existing atlas remains usable
without downloading or regenerating its inputs.

## Host checks

Run `idf.py reconfigure` first if managed dependencies have not yet been restored.
Create a separate Python environment and install
`tools/requirements-assets.txt`, then run:

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
cc -Wall -Wextra -Werror -I main tests/rotation.c -o /tmp/adsb-rotation-test
/tmp/adsb-rotation-test
```

The suite covers atlas/city integrity and projection, aircraft data, navigation,
the actual LVGL shell, storage lifecycle failures, BSP initialization failures
and packaging. The accepted release passed 19 host tests and the rotation check;
normal and independent builds from defaults produced identical firmware/configuration.

## Packaging

After building, assemble into ignored build directories using the app-repository
validator schema:

```sh
python tools/package.py --output build/package --records build/release \
  --icons assets/icons --revision 11 --version 0.3.2
```

Packaging uses the bundled `tools/package.schema.json`, a snapshot of the
app-repository validator schema. Pass `--schema PATH` to validate against another
local copy of that schema.

Generated metadata derives `bsp_version` from the resolved lockfile. The package
contains the current SD assets, icon sizes, executable and license notices.
Increment the version and revision for a subsequent release. Keep generated
packages, logs and build records under `build/` or outside the source checkout.

## License and provenance

The application is derived from [the original ADS-B display](https://github.com/iamneilroberts/adsb)
and is distributed under the [MIT license](LICENSE). Local HAL/BSP sources and
map/font data retain their required upstream copyright and license notices.
`validation.json` describes the previously accepted release build; it is not a
claim that a new firmware image has been built from this checkout.
