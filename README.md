# Pimoroni Presto — Zephyr Port

Out-of-tree [Zephyr RTOS](https://zephyrproject.org/) board support and example apps for the [Pimoroni Presto](https://shop.pimoroni.com/products/presto): a 4-inch 480×480 touchscreen with RP2350B, 8 MB PSRAM, RM2/CYW43439 Wi-Fi/BT, 7× SK6812 RGB LEDs, microSD, and piezo speaker.

> **Display status: hardware-validated.** The Presto's ST7701 panel runs in 18bpp parallel-RGB (DPI) mode. Upstream Zephyr has no driver for this, so this repo ships an out-of-tree one (`drivers/presto/`) ported from the MIT-licensed Pimoroni firmware: two RP2350 PIO1 state machines (pixel data + sync timing) plus a DMA pair stream a 480×480 RGB565 framebuffer out of SRAM. It builds clean, runs on `native_sim` via Zephyr's SDL display, and has been **confirmed on real hardware** (`test_display` colour bars + animated square render correctly over SWD). The 7× SK6812 NeoPixels likewise need an out-of-tree driver (a `drivers/presto` fork of the WS2812 PIO driver, for the RP2350B >GP31 data pin) and are HW-validated. The rest — capacitive touch, Wi-Fi, microSD, piezo, user button — is brought up with stock Zephyr drivers.

Modelled on [`beriberikix/tufty2350-zephyr`](https://github.com/beriberikix/tufty2350-zephyr): same layout, same Zephyr revision pin (v4.4.0), same UF2-drop flash workflow.

## Status

| Subsystem | State | Notes |
|---|---|---|
| RP2350B SoC | ✅ working | UF2 boots, GPIO/I2C/PIO/DMA/PWM all initialise |
| USER_SW (GP46) | ✅ working | Shared with BOOTSEL; usable as runtime input |
| 7× SK6812 NeoPixels (GP33) | ✅ working | HW-validated. `pimoroni,ws2812-presto-pio` (a `drivers/presto` fork of the upstream WS2812 PIO driver) on PIO2 — PIO0 hosts Wi-Fi, PIO1 the display, so all three coexist. The fork adds the RP2350B fixes the upstream driver lacks for a data pin >GP31 (`pio_set_gpio_base(16)` + an absolute `out-pin`) |
| FT6236 cap touch | ✅ working | HW-validated (x/y down/up events). On a **software bit-bang I2C** bus (`gpio-i2c`, GP30/31): the panel clock-stretches and the RP2350 hardware I2C locks up on it; bit-bang tolerates it, like Pimoroni's MicroPython |
| CYW43439 Wi-Fi (RM2) | ✅ working | `infineon,airoc-wifi` over PIO-SPI; HW-validated (firmware loads, MAC read, netif up). Opt-in via overlay; needs ≥4 KB stacks |
| Qw/ST I2C0 (GP40/41) | ✅ working | Use for external breakouts |
| Piezo (GP43 PWM) | ⚠️ DTS reserved | Driver not wired into an app yet |
| Display ST7701 | ✅ working | Out-of-tree `drivers/presto` (PIO+DMA DPI scanout); HW-validated (colour bars + animated square). Single-buffered (can tear). Optional half-res 240×240 mode (`CONFIG_ST7701_PRESTO_HALF_RES`) pixel-doubles to the panel from a 115 KB framebuffer |
| Wi-Fi **+** display together | ✅ working | HW-validated: with `CONFIG_ST7701_PRESTO_HALF_RES` the framebuffer drops to 115 KB, leaving SRAM for the full net stack. `wifi_display` associates + DHCP + DNS + HTTP GET with the panel rendering throughout (lease `192.168.x.y` read back over SWD). Full-res FB + Wi-Fi does **not** fit, and the framebuffer cannot scan out of PSRAM over the shared QMI bus |
| 8 MB PSRAM (GP47 CS) | ✅ working | Out-of-tree QMI window-1 init (`drivers/presto/drivers/memc`); mapped at `0x11000000`, HW-validated, exposed via mem-attr heap |
| microSD (GP34-39) | ✅ working | HW-validated for **read**: card detected, FAT mounted, root listed. Driven in SPI mode on the hardware SPI0 controller (SCLK=GP34, MOSI=GP35, MISO=GP36) with the card's DAT3 (GP39) as a GPIO chip select. Write is implemented and exercised by `test_sdcard` but not yet confirmed on hardware. Disabled by default; see `apps/test_sdcard` |

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
├── drivers/presto/                  # Out-of-tree Zephyr module
│   ├── zephyr/module.yml            # Registers the module (cmake + kconfig + dts_root)
│   ├── dts/bindings/display/        # pimoroni,st7701-presto.yaml
│   ├── dts/bindings/led_strip/      # pimoroni,ws2812-presto-pio.yaml
│   ├── drivers/display/             # ST7701 DPI driver + ported PIO programs (.pio.h)
│   ├── drivers/led_strip/           # WS2812 PIO driver fork (RP2350B GP33 fixes)
│   └── drivers/memc/                # APS6404 PSRAM driver
├── apps/
│   ├── test_leds/                   # 7× SK6812 colour cycle via PIO
│   ├── test_buttons/                # USER_SW edge logger
│   ├── test_touch/                  # FT6236 events via INPUT subsystem
│   ├── test_wifi/                   # CYW43439 default-iface bring-up
│   ├── test_display/                # ST7701 colour bars + animated square (SDL on native_sim)
│   ├── test_psram/                  # 8 MB PSRAM detect + full RW test + heap alloc
│   ├── wifi_display/                # Wi-Fi connect + DHCP + HTTP GET, status on the panel
│   └── kitchen_sink/                # 5 feature screens on the panel, touch-swipe nav
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
| PSRAM | 8 MB APS6404 on dedicated CS (GP47), mapped at `0x11000000` via QMI window 1 |
| Display | 4″ 480×480 IPS, ST7701, 18bpp parallel RGB + 9-bit SPI cmd bus — driven by `drivers/presto` (PIO+DMA DPI scanout) |
| Touch | FT6236 capacitive, addr 0x48, software bit-bang I2C on GP30/31 (panel clock-stretches; RP2350 HW I2C locks up) |
| LEDs | 7× SK6812 NeoPixels, GP33 (PIO2) |
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

### Wi-Fi *and* the display together

The full-res 480×480 framebuffer is ~450 KB of the 520 KB SRAM — too much to also
fit the networking stack (a full-res + Wi-Fi build overflows RAM by ~42 KB). The
framebuffer **cannot** be moved to PSRAM: scanning it out over the QMI (shared with
flash code fetch) underruns and tears — hardware-confirmed, and the reason
Pimoroni's firmware keeps it in SRAM.

The solution (mirroring Pimoroni's `full_res=false`) is `CONFIG_ST7701_PRESTO_HALF_RES=y`:
the panel is driven from a 240×240 framebuffer (115 KB, in SRAM) that is pixel- and
line-doubled in the scanout, leaving ~335 KB free for Wi-Fi. The `wifi_display` app
wires this together — half-res framebuffer + CYW43439 connect + DHCP + DNS + HTTP
GET, with the panel showing the connection state (amber → blue → cyan → green):

```bash
# Put your AP credentials in an untracked conf (gitignored):
cat > apps/wifi_display/wifi_creds.conf <<'EOF'
CONFIG_WIFI_DISPLAY_SSID="my-network"
CONFIG_WIFI_DISPLAY_PSK="my-password"
EOF

cmake -S apps/wifi_display -B build/wifi_display -GNinja \
  -DBOARD=presto/rp2350b/m33 \
  -DEXTRA_CONF_FILE="$PWD/apps/wifi_display/wifi_creds.conf" \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/wifi_display
```

Half-res scans out of SRAM (the proven path), so the panel stays clean while the
radio runs. Effective resolution is halved; panel timing/refresh is unchanged.

## Flash

Hold the **BOOT** button on the Presto while plugging in USB to enter the RP2350 UF2 bootloader. The board appears as a USB drive named `RP2350`:

```bash
cp build/$APP/zephyr/zephyr.uf2 /media/$USER/RP2350/
```

The board resets and runs the new firmware. To re-enter the bootloader later, hold BOOT and press the reset/run line (or unplug-replug while holding BOOT).

Alternative runners are wired up in [`boards/pimoroni/presto/board.cmake`](boards/pimoroni/presto/board.cmake):

- `west flash --runner uf2` — auto-detect the mounted UF2 drive
- `west flash --runner openocd` — via CMSIS-DAP probe (e.g. Raspberry Pi Debug Probe / Picoprobe)
- `west flash --runner probe-rs` — via probe-rs (`--chip=RP235x`)

**openocd note (RP2350):** the Zephyr SDK's bundled openocd has no `target/rp2350.cfg`, so point west at an openocd with RP2350 support — e.g. the one in the pico-sdk. Pass both the binary and its scripts dir (west's own `-s` paths otherwise shadow openocd's built-in default):

```bash
west flash --runner openocd \
  --openocd "$HOME/.pico-sdk/openocd/0.12.0+dev/openocd" \
  --openocd-search "$HOME/.pico-sdk/openocd/0.12.0+dev/scripts"
```

(`checking adapter speed...` / a one-line `BUG: unknown adapter clock mode` are harmless — the board's `support/openocd.cfg` queries the speed before setting it.) The debug probe only provides SWD, not power — the Presto must be powered over its own USB-C, otherwise openocd reports `Error connecting DP: cannot read IDR`.

## Apps

| App | What it does | Hardware exercised |
|---|---|---|
| `test_leds` | Cycles the 7-LED chain through R / G / B / off | WS2812-PIO on PIO2 |
| `test_buttons` | Polls USER_SW every 20 ms, logs press/release edges | GPIO, BOOTSEL-shared button |
| `test_touch` | Subscribes to INPUT events from the FT6236, logs (x, y, pressed) | I2C1, INPUT subsystem |
| `test_wifi` | Acquires the default network interface; placeholder for scan | CYW43439 over PIO-SPI |
| `test_display` | Draws RGB565 colour bars + an animated square via the display API | ST7701 (board) / SDL (`native_sim`) |
| `test_psram` | Detects the 8 MB PSRAM, walks the full device (address/pattern/walking-bit tests), allocates from the PSRAM heap | APS6404 over QMI window 1 |
| `wifi_display` | Half-res display + Wi-Fi: associates to an AP, takes a DHCP lease, resolves a name + HTTP GET, and tracks each phase as a status colour on the panel | ST7701 + CYW43439 **together** |
| `test_sdcard` | Initialises the microSD over SPI0, reports its geometry, mounts FAT, lists the root and round-trips a file | SPI0, SDHC, FATFS |
| `kitchen_sink` | Five feature screens (neopixel / button / touch / wifi / psram) rendered on the ST7701 with an 8x8 bitmap font; navigate by **touch swipe** or USER_SW. Half-res + double-buffered so the display, Wi-Fi and LEDs run together. Wi-Fi screen needs credentials — same untracked-conf pattern as `wifi_display` but with `CONFIG_KITCHEN_SINK_WIFI_SSID`/`_PSK` (else it shows "(no SSID set)") | All of the above |

Each app has the same shape:

```
apps/<name>/
├── CMakeLists.txt              # Adds BOARD_ROOT, includes Zephyr, lists sources
├── prj.conf                    # Base Kconfig
├── boards/
│   ├── native_sim_native_64.conf      # Disables hardware drivers for emulation
│   ├── native_sim_native_64.overlay   # Stubs (sw0 button on gpio_emul, etc.)
│   └── presto_rp2350b_m33.overlay      # Hardware-only opt-ins (e.g. Wi-Fi)
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

### Testing graphics on native_sim

`test_display` targets the `chosen zephyr,display` device, so on `native_sim` it draws into Zephyr's **SDL display emulator** — letting you exercise the drawing/app layer (not the ST7701 PIO/DMA driver itself) without hardware:

```bash
west build -p always -b native_sim/native/64 apps/test_display
./build/zephyr/zephyr.exe                 # opens an SDL window with colour bars + a moving square
SDL_VIDEODRIVER=offscreen ./build/zephyr/zephyr.exe   # headless (no window), for CI
```

Requires `libsdl2-dev`. The smoke script runs it with the `offscreen` driver so it works on headless hosts.

## Pin map (reference)

GPIOs are RP2350B GPIO numbers. In DTS, `&gpio0` covers GP0-31 and `&gpio0_hi` covers GP32-47 (so GP32 = `gpio0_hi` index 0, GP46 = index 14).

| Peripheral | Signal | GPIO | DT label |
|---|---|---|---|
| Display data (RGB565, 16 lanes) | parallel RGB | GP1-GP16 | `&st7701` (GP17/18 tied low) |
| Display timing | HSYNC / VSYNC / DE / PCLK | GP19 / GP20 / GP21 / GP22 | `&st7701` (PIO1) |
| Display cmd bus (9-bit, bit-bang) | CLK / DATA / CS | GP26 / GP27 / GP28 | `&st7701` |
| Display reset | RESET | GP44 | *unused (SWRESET command)* |
| Backlight | BACKLIGHT_EN | GP45 | `&st7701` (GPIO on/off) |
| Wi-Fi (CYW43439) | REG_ON / DATA / CS / CLK | GP23 / GP24 / GP25 / GP29 | `&airoc_wifi` (disabled by default) |
| Touch (FT6236) | SDA / SCL / INT / RESET | GP30 / GP31 / GP32 / GP42 | `&ft6236` on `&touch_i2c` (bit-bang; HW `&i2c1` disabled) |
| NeoPixels | LED_DATA | GP33 | `&ws2812` (alias `led-strip`) |
| microSD | SCLK / CMD / DAT0 / DAT3-CS | GP34 / GP35 / GP36 / GP39 | `&sdhc0` on `&spi0` (disabled by default) |
| Qw/ST I2C | SDA / SCL | GP40 / GP41 | `&i2c0` |
| Piezo audio | PWM | GP43 | *reserved* |
| USER_SW | input | GP46 | `&user_sw` (alias `sw0`) |
| PSRAM | CS | GP47 | `&psram` (QMI CS1) |

## Troubleshooting

**`devicetree error: ... parse error`** — make sure pin numbers in board files use `gpio0` (GP0-31) vs `gpio0_hi` (GP32-47). The hi-bank index is `gpio - 32` (e.g. GP46 → `<&gpio0_hi 14 ...>`).

**`undefined reference to k_malloc`** in a Wi-Fi build — the Infineon HAL needs a heap pool. Add `CONFIG_HEAP_MEM_POOL_SIZE=16384` to your `prj.conf`.

**`file SIZE requested of path that is not readable: ... clm_blob`** — the CYW43439 firmware blob isn't fetched yet. Run `west blobs fetch hal_infineon`.

**`UART0 garbled when display enabled`** — by design: UART0 (GP0/GP1) shares pins with the display's B7/B6 data lanes. Use a debug probe (e.g. Picoprobe on the JST-SH connector available from June 2025 onwards) instead, or move the console to USB CDC ACM (`CONFIG_USB_DEVICE_STACK=y` + `CONFIG_USB_CDC_ACM=y` + `chosen { zephyr,console = &cdc_acm_uart0; };`).

**USB CDC console prints nothing** — Zephyr's CDC ACM discards output until the host raises DTR, and not every terminal does (`tio` on macOS does not). A perfectly healthy board is then indistinguishable from a hung one: the port enumerates, the terminal connects, and zero bytes arrive. Assert DTR explicitly before reading — `scripts/cdccap.py /dev/cu.usbmodemXXXX 20` does this and dumps the console. Note also that `CONFIG_HWINFO=n` is currently required alongside `-S cdc-acm-console` on RP2350: Zephyr main's `hwinfo_rpi_pico.c` references `POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_RSM_BITS` while the `hal_rpi_pico` revision Zephyr itself pins only defines the `_PSM` spelling, so anything that pulls in HWINFO fails to compile.

**`west update` is slow** — that's fetching ~700 MB of Zephyr modules including HALs and tooling. Subsequent updates are incremental.

**LED strip builds but doesn't light up** — verify the chain length matches the hardware (7 on a stock Presto) and that the GPIO is in PIO function mode (handled by the `&ws2812_pio2_default` pinctrl group). On RP2350B the data line is GP33 (>GP31), which the stock `worldsemi,ws2812-rpi_pico-pio` driver can't reach; the board uses the `pimoroni,ws2812-presto-pio` fork (`out-pin = <33>`, `pio_set_gpio_base(16)`) instead.

## Known limitations

- **Display**: implemented out-of-tree in `drivers/presto` (RGB565 DPI scanout via two PIO1 SMs + a per-line DMA pair, ported from [`pimoroni/presto:drivers/st7701`](https://github.com/pimoroni/presto/tree/main/drivers/st7701)). **Hardware-validated**: PCLK/sync timing, lane→colour mapping and the COLMOD-0x66/16-lane combo confirmed on a panel. Note the scanout consumes **byte-swapped RGB565**, matching Pimoroni's PicoGraphics convention — `display_write()` takes standard little-endian RGB565 and byteswaps on the way in, but `get_framebuffer()` returns the raw byte-swapped buffer (direct writers must byteswap themselves). Single-buffered by default, so full-frame `display_write` can tear; `CONFIG_ST7701_PRESTO_DOUBLE_BUFFER` adds a back buffer and a `st7701_presto_flip()` that swaps at the vertical blank (tear-free, HW-validated). Double-buffering needs a second framebuffer, so it pairs with half-res (two 240×240 buffers fit in SRAM; two full-res buffers fail a BUILD_ASSERT). The full-res framebuffer is ~450 KB in SRAM, leaving little room for large concurrent workloads. To run the networking stack alongside the display, `CONFIG_ST7701_PRESTO_HALF_RES=y` drives the panel from a 240×240 framebuffer (115 KB, pixel-/line-doubled in scanout) — see `wifi_display`. The framebuffer **cannot** be relocated to PSRAM: scanning it out over the QMI (shared with flash XIP) underruns and tears (hardware-confirmed, both cached and uncached aliases), which is why Pimoroni keeps it in SRAM too.
- **PSRAM**: brought up out-of-tree in `drivers/presto/drivers/memc` (QMI window-1 init ported from the MIT-licensed MicroPython `rp2_psram.c`). The 8 MB is mapped at `0x11000000` at boot (`POST_KERNEL`, after `clk_sys` is up — same ordering as Pimoroni's firmware) and exposed as a `zephyr,memory-region` with a mem-attr heap (`CONFIG_MEM_ATTR_HEAP` → `mem_attr_heap_alloc(DT_MEM_SW_ALLOC_DMA, ...)`). The region is cacheable for CPU use; DMA producers/consumers must do XIP cache maintenance (`0x18000000`). See `apps/test_psram`.
- **microSD**: driven in SPI mode on the hardware SPI0 controller, not the 4-bit SDIO the slot is wired for. The RP2350 has no SD host controller, so SDIO would need a PIO implementation, and all three PIO blocks are spoken for (PIO0 Wi-Fi, PIO1 display, PIO2 LEDs). SPI costs roughly a quarter of the bandwidth and no PIO at all. GP34/35/36 land on SPI0 by luck of the RP2350B pin table — SPI blocks alternate every eight pins, and GP32-39 is a SPI0 block whose {SCK, TX, RX} fall exactly on the three lines SPI mode needs. The chip select is a plain GPIO rather than the PL022's own, because the SD SPI protocol needs CS held low across a whole multi-block command sequence and the hardware CS deasserts between transfers. **All three nodes (`&spi0`, `&sdhc0`, `&sdmmc0`) must be enabled explicitly** — Zephyr decides a node's status without looking at its parent, so a slot left `okay` under a disabled bus still instantiates its driver and fails at link with an undefined `__device_dts_ord_N` that mentions nothing about SPI.
- **USER_SW** shares the physical button with QSPI BOOTSEL. Pressing it at reset enters the UF2 bootloader; pressing it at runtime fires an INPUT event.
- **UART0 conflict** (see Troubleshooting).
- **No IMU, no external RTC**: Apps the Tufty port has for these (`test_imu`, `test_rtc`) are intentionally not ported — the Presto has neither.

## Roadmap

Likely next steps, roughly in order of value:

1. **Display driver** — ✅ done and hardware-validated (`drivers/presto`, see `test_display`), including a half-res mode and tear-free double-buffering (`st7701_presto_flip`). Remaining: optional LVGL support.
2. **PSRAM bring-up** — ✅ done (`drivers/presto/drivers/memc`, see `apps/test_psram`); 8 MB at `0x11000000` via a mem-attr heap.
3. **microSD SPI block device** — ✅ done (`apps/test_sdcard`); read path HW-validated, write path still to confirm.
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
