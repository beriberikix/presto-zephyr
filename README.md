# Pimoroni Presto — Zephyr Port

Out-of-tree [Zephyr RTOS](https://zephyrproject.org/) board support and example apps for the [Pimoroni Presto](https://shop.pimoroni.com/products/presto): a 4-inch 480×480 touchscreen with RP2350B, 8 MB PSRAM, RM2/CYW43439 Wi-Fi/BT, 7× SK6812 RGB LEDs, microSD, and piezo speaker.

> **Display status: not yet supported.** The Presto's ST7701 panel runs in 18bpp parallel-RGB mode driven by three custom PIO programs in the vendor firmware. Upstream Zephyr has no driver for this. The display pins are reserved in the devicetree but the panel node is disabled with a TODO. Everything else — NeoPixels, capacitive touch, Wi-Fi, microSD, piezo, user button — is brought up with stock Zephyr drivers.

Modelled on [`beriberikix/tufty2350-zephyr`](https://github.com/beriberikix/tufty2350-zephyr): same layout, same Zephyr revision pin (v4.4.0), same UF2-drop flash workflow.

## Status

| Subsystem | State | Notes |
|---|---|---|
| RP2350B SoC | ✅ working | UF2 boots, GPIO/I2C/PIO/DMA/PWM all initialise |
| USER_SW (GP46) | ✅ working | Shared with BOOTSEL; usable as runtime input |
| 7× SK6812 NeoPixels (GP33) | ✅ working | `worldsemi,ws2812-rpi_pico-pio` on PIO0 SM3 |
| FT6236 cap touch (I2C1) | ✅ wired in DTS | `focaltech,ft6146` driver — verify on real HW |
| CYW43439 Wi-Fi (RM2) | ✅ builds, fetches blob | `infineon,airoc-wifi` over PIO-SPI; opt-in via overlay |
| Qw/ST I2C0 (GP40/41) | ✅ working | Use for external breakouts |
| Piezo (GP43 PWM) | ⚠️ DTS reserved | Driver not wired into an app yet |
| Display ST7701 | ❌ TODO | No upstream Zephyr driver; pins reserved |
| 8 MB PSRAM (GP47 CS) | ❌ TODO | QMI window 1 init not exposed by Zephyr |
| microSD (GP34-39) | ❌ TODO | Wired for 4-bit SDIO; SPI mode not yet enabled |

## Layout

```
.
├── boards/pimoroni/presto/          # Board files: DTS, pinctrl, Kconfig, runners
│   ├── board.yml
│   ├── board.cmake                  # openocd, probe-rs, uf2 runners
│   ├── Kconfig.presto
│   ├── Kconfig.defconfig
│   ├── presto.dtsi                  # Peripherals, aliases, chosen, pin map (annotated)
│   ├── presto-pinctrl.dtsi          # Pin function selection
│   ├── presto_rp2350b_m33.dts       # Top-level board DTS
│   ├── presto_rp2350b_m33.yaml      # Twister metadata
│   └── presto_rp2350b_m33_defconfig
├── apps/
│   ├── test_leds/                   # 7× SK6812 colour cycle via PIO
│   ├── test_buttons/                # USER_SW edge logger
│   ├── test_touch/                  # FT6236 events via INPUT subsystem
│   ├── test_wifi/                   # CYW43439 default-iface bring-up
│   └── kitchen_sink/                # 4-screen cycler (auto + USER_SW)
├── scripts/
│   └── smoke_native_sim.sh          # Builds all apps for native_sim, runs 3 s each
├── CMakeLists.txt                   # Top-level placeholder
├── west.yml                         # Pins Zephyr v4.4.0
├── LICENSE
└── README.md
```

## Hardware summary

| Feature | Detail |
|---|---|
| MCU | RP2350B (dual Cortex-M33 @ 150 MHz, 520 KB SRAM) |
| Flash | 16 MB QSPI (W25Q128) |
| PSRAM | 8 MB APS6404 on dedicated CS (GP47) — *not currently initialised* |
| Display | 4″ 480×480 IPS, ST7701, 18bpp parallel RGB + 9-bit SPI cmd bus — *disabled in this port* |
| Touch | FT6236 capacitive, I2C1, addr 0x48 |
| LEDs | 7× SK6812 NeoPixels, GP33 (PIO0 SM3) |
| Wireless | RM2 module (CYW43439) — Wi-Fi b/g/n + BT, PIO-SPI |
| Storage | microSD slot, SDIO-capable |
| Audio | Piezo speaker on GP43 (PWM) |
| User input | USER_SW on GP46 (shared with QSPI BOOTSEL) |
| Expansion | Qw/ST (Qwiic/STEMMA QT) on I2C0 (GP40/41) |
| Power | USB-C, JST-PH battery (3-5.5 V) — no on-board charger |

Full pin map and per-peripheral notes are inline at the top of [`boards/pimoroni/presto/presto.dtsi`](boards/pimoroni/presto/presto.dtsi).

## Prerequisites

- **Zephyr SDK** ≥ 1.0.1 — [install guide](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html)
- **CMake** ≥ 3.20, **Ninja**, **Python** ≥ 3.10
- For `native_sim` smoke runs: `sudo apt install libsdl2-dev`

## Setup

This repo is structured as a Zephyr [T2 manifest](https://docs.zephyrproject.org/latest/develop/west/workspaces.html#t2-star-topology-application-is-the-manifest-repository): cloning it and running `west update` pulls Zephyr v4.4.0 and all its required modules into the same directory.

```bash
# 1. Clone this repo as the workspace root
git clone https://github.com/beriberikix/presto-zephyr
cd presto-zephyr

# 2. Create a Python venv and install west
python3 -m venv .venv
source .venv/bin/activate
pip install west

# 3. Initialise the workspace using this repo's manifest
west init -l .
west update          # ~5 min: fetches Zephyr v4.4.0 and modules
west zephyr-export

# 4. Install Zephyr's Python dependencies
pip install -r zephyr/scripts/requirements.txt

# 5. Fetch binary blobs needed by the CYW43439 Wi-Fi driver
west blobs fetch hal_infineon

# 6. Export Zephyr base for cmake
export ZEPHYR_BASE="$PWD/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"  # adjust to your SDK
```

After `west update`, the directory will also contain `zephyr/`, `modules/`, `bootloader/`, `tools/`, and `.west/` — all gitignored.

## Build

The apps each invoke `find_package(Zephyr)` and append this repo's `boards/` to `BOARD_ROOT`, so plain `cmake` works without `west build`.

```bash
# Pick an app
APP=test_leds

cmake -S apps/$APP -B build/$APP -GNinja \
  -DBOARD=presto/rp2350b/m33 \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/$APP
```

Output: `build/$APP/zephyr/zephyr.uf2` (≈ 70 KB for the small apps, ≈ 1.2 MB for `kitchen_sink` with Wi-Fi).

### Wi-Fi (opt-in)

Wi-Fi is off by default to keep the small apps small. Enable it per-app:

```bash
cmake -S apps/test_wifi -B build/test_wifi -GNinja \
  -DBOARD=presto/rp2350b/m33 \
  -DDTC_OVERLAY_FILE="$PWD/apps/test_wifi/boards/presto_rp2350b_m33.overlay" \
  -DEXTRA_CONF_FILE="$PWD/apps/test_wifi/prj_wifi.conf" \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/test_wifi
```

The overlay flips `&airoc_wifi { status = "okay"; }`; `prj_wifi.conf` adds `CONFIG_WIFI_AIROC=y` and the CYW43439 driver Kconfigs.

## Flash

Hold the **BOOT** button on the Presto while plugging in USB to enter the RP2350 UF2 bootloader. The board appears as a USB drive named `RP2350`:

```bash
cp build/$APP/zephyr/zephyr.uf2 /media/$USER/RP2350/
```

The board resets and runs the new firmware. To re-enter the bootloader later, hold BOOT and press the reset/run line (or unplug-replug while holding BOOT).

Alternative runners are wired up in [`boards/pimoroni/presto/board.cmake`](boards/pimoroni/presto/board.cmake):

- `west flash --runner uf2` — auto-detect the mounted UF2 drive
- `west flash --runner openocd` — via CMSIS-DAP probe (e.g. Picoprobe)
- `west flash --runner probe-rs` — via probe-rs (`--chip=RP235x`)

## Apps

| App | What it does | Hardware exercised |
|---|---|---|
| `test_leds` | Cycles the 7-LED chain through R / G / B / off | WS2812-PIO on PIO0 SM3 |
| `test_buttons` | Polls USER_SW every 20 ms, logs press/release edges | GPIO, BOOTSEL-shared button |
| `test_touch` | Subscribes to INPUT events from the FT6236, logs (x, y, pressed) | I2C1, INPUT subsystem |
| `test_wifi` | Acquires the default network interface; placeholder for scan | CYW43439 over PIO-SPI |
| `kitchen_sink` | Cycles through neopixel / button / touch / wifi "screens" every 5 s (or on USER_SW press) | All of the above |

Each app has the same shape:

```
apps/<name>/
├── CMakeLists.txt              # Adds BOARD_ROOT, includes Zephyr, lists sources
├── prj.conf                    # Base Kconfig
├── boards/
│   ├── native_sim.conf         # Disables hardware drivers for emulation
│   ├── native_sim.overlay      # Stubs (sw0 button on gpio_emul, etc.)
│   └── presto_rp2350b_m33.overlay   # Hardware-only opt-ins (e.g. Wi-Fi)
└── src/main.c
```

## native_sim smoke test

To sanity-check devicetree and Kconfig changes without a board attached:

```bash
./scripts/smoke_native_sim.sh
```

This builds every app for `native_sim/native/64` and runs each `zephyr.exe` for 3 seconds. Apps that depend on missing hardware (no LED strip, no FT6236, no Wi-Fi on the host) detect that via `IS_ENABLED(...)` guards and log a "not enabled" line instead of crashing.

Override defaults via env vars:

```bash
SMOKE_TIMEOUT_SECONDS=10 BOARD=native_sim/native/64 ./scripts/smoke_native_sim.sh
```

## Pin map (reference)

GPIOs are RP2350B GPIO numbers. In DTS, `&gpio0` covers GP0-31 and `&gpio0_hi` covers GP32-47 (so GP32 = `gpio0_hi` index 0, GP46 = index 14).

| Peripheral | Signal | GPIO | DT label |
|---|---|---|---|
| Display data (B/G/R, 18 lanes) | parallel RGB | GP1-GP18 | *reserved (display disabled)* |
| Display timing | HSYNC / VSYNC / DE / PCLK | GP19 / GP20 / GP21 / GP22 | *reserved* |
| Display cmd bus (9-bit SPI) | CLK / DATA / CS | GP26 / GP27 / GP28 | *reserved* |
| Display reset | RESET | GP44 | *reserved* |
| Backlight | BACKLIGHT_EN (PWM) | GP45 | *reserved* |
| Wi-Fi (CYW43439) | REG_ON / DATA / CS / CLK | GP23 / GP24 / GP25 / GP29 | `&airoc_wifi` (disabled by default) |
| Touch (FT6236) | SDA / SCL / INT / RESET | GP30 / GP31 / GP32 / GP42 | `&ft6236` on `&i2c1` |
| NeoPixels | LED_DATA | GP33 | `&ws2812` (alias `led-strip`) |
| microSD | SCLK / CMD / DAT0-3 | GP34 / GP35 / GP36-39 | *reserved* |
| Qw/ST I2C | SDA / SCL | GP40 / GP41 | `&i2c0` |
| Piezo audio | PWM | GP43 | *reserved* |
| USER_SW | input | GP46 | `&user_sw` (alias `sw0`) |
| PSRAM | CS | GP47 | *reserved* |

## Troubleshooting

**`devicetree error: ... parse error`** — make sure pin numbers in board files use `gpio0` (GP0-31) vs `gpio0_hi` (GP32-47). The hi-bank index is `gpio - 32` (e.g. GP46 → `<&gpio0_hi 14 ...>`).

**`undefined reference to k_malloc`** in a Wi-Fi build — the Infineon HAL needs a heap pool. Add `CONFIG_HEAP_MEM_POOL_SIZE=16384` to your `prj.conf`.

**`file SIZE requested of path that is not readable: ... clm_blob`** — the CYW43439 firmware blob isn't fetched yet. Run `west blobs fetch hal_infineon`.

**`UART0 garbled when display enabled`** — by design: UART0 (GP0/GP1) shares pins with the display's B7/B6 data lanes. Use a debug probe (e.g. Picoprobe on the JST-SH connector available from June 2025 onwards) instead, or move the console to USB CDC ACM (`CONFIG_USB_DEVICE_STACK=y` + `CONFIG_USB_CDC_ACM=y` + `chosen { zephyr,console = &cdc_acm_uart0; };`).

**`west update` is slow** — that's fetching ~700 MB of Zephyr modules including HALs and tooling. Subsequent updates are incremental.

**LED strip builds but doesn't light up** — verify the chain length matches the hardware (7 on a stock Presto) and that the GPIO is in PIO function mode (handled by `&ws2812_pio0_default` pinctrl group).

## Known limitations

- **Display**: ST7701 in 18bpp parallel-RGB mode is not supported by upstream Zephyr. Vendor reference uses 3 custom PIO programs + a ping-pong DMA chain (see [`pimoroni/presto:drivers/st7701/*.pio`](https://github.com/pimoroni/presto/tree/main/drivers/st7701)).
- **PSRAM**: GP47 has a dedicated PSRAM CS option on the RP2350B, but mapping the chip into the QMI window 1 XIP region requires direct register writes the Zephyr RP2350 HAL doesn't currently expose.
- **microSD**: Wired for 4-bit SDIO; this initial port leaves it disabled. SPI-mode bring-up is straightforward but not yet wired into an app.
- **USER_SW** shares the physical button with QSPI BOOTSEL. Pressing it at reset enters the UF2 bootloader; pressing it at runtime fires an INPUT event.
- **UART0 conflict** (see Troubleshooting).
- **No IMU, no external RTC**: Apps the Tufty port has for these (`test_imu`, `test_rtc`) are intentionally not ported — the Presto has neither.

## Roadmap

Likely next steps, roughly in order of value:

1. **Display driver** — write a Zephyr display driver wrapping the vendor's PIO+DMA approach to ST7701. This is the biggest missing piece.
2. **PSRAM bring-up** — QMI window 1 init at boot to expose the 8 MB as `&psram0`.
3. **microSD SPI block device** — enable `zephyr,sdhc-spi-slot` with CS on GP39 and mount FAT.
4. **USB CDC ACM console** — so you don't lose stdio when the display is wired up.
5. **Piezo audio driver** — wire GP43 into the `audio` subsystem (sound, beeps, simple synth).

## Contributing

Issues and PRs welcome. The simplest first contribution is verifying any of the ⚠️/❌ items above on real hardware.

## References

- [Pimoroni Presto product page](https://shop.pimoroni.com/products/presto)
- [Presto schematic (PDF)](https://cdn.shopify.com/s/files/1/0174/1800/files/pico_presto_schematic.pdf)
- [Pimoroni's MicroPython firmware](https://github.com/pimoroni/presto) — canonical source of pin assignments
- [Pimoroni's C++ boilerplate](https://github.com/pimoroni/presto-boilerplate)
- [Tufty 2350 Zephyr port](https://github.com/beriberikix/tufty2350-zephyr) — structural template for this repo
- [Zephyr documentation](https://docs.zephyrproject.org/)
- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

## License

MIT — see [LICENSE](LICENSE).
