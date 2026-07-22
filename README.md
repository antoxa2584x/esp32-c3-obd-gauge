# ESP32 WiFi OBD-II Gauge

A standalone automotive gauge on the **ESP32-2424S012** (ESP32-C3 + 1.28"
round GC9A01 240×240 IPS + CST816 capacitive touch) that talks to an
**ELM327 WiFi V1.5 (PIC18F25K80)** adapter over WiFi — no phone required.

## Screens

| Screen | What it shows |
|--------|---------------|
| **WiFi picker** | Scans for networks at boot; pick your adapter's SSID (or **Demo**) |
| **Connect** | Live status while joining the adapter and initializing OBD |
| **RPM** | Engine RPM arc gauge (green→amber→red zones) + speed readout |
| **Coolant** | Coolant temperature arc gauge + overheat warning overlay |
| **Trouble Codes** | Scan (mode 03) and clear (mode 04) stored DTCs |
| **Settings** | Dark/light theme + metric/imperial units (saved to flash) |

## Controls (touch)

| Gesture | Action |
|---------|--------|
| **Swipe ← / →** | previous / next screen |
| **Swipe ↑ / ↓** | brightness up / down (saved to flash) |
| **Hold** a gauge (long-press) | open its picker — max RPM (RPM screen) / warning temp (Coolant screen) |

There are no on-screen arrows — navigation is swipe-only. A quick swipe never
triggers the long-press picker.

## How it works

The ESP32-C3 joins the ELM327's WiFi access point as a station, opens a TCP
socket to `192.168.0.10:35000`, runs the ELM327 AT handshake, then round-robin
polls OBD-II mode-01 PIDs. Everything runs cooperatively on the single core —
the OBD client is a **non-blocking state machine** so WiFi and the LVGL UI
never stall.

```
ESP32-2424S012  ──WiFi(STA→AP)──▶  ELM327 V1.5  ──OBD-II──▶  Car ECU
 GC9A01 + touch      TCP :35000     192.168.0.10
```

## Required libraries (Arduino IDE)

| Library | Version | Notes |
|---------|---------|-------|
| **esp32** by Espressif (Boards Manager) | 3.x | ESP32-C3 core + WiFi + Preferences + Wire |
| **LVGL** | **8.3.x / 8.4.x** | *not* 9.x — this code targets the v8 API |
| **LovyanGFX** | 1.1.x+ | GC9A01 SPI display driver (touch is driven directly, see below) |

## Board & build settings

- **Board:** `ESP32C3 Dev Module`
- **Partition Scheme:** **`Huge APP (3MB No OTA/1MB SPIFFS)`** ← *required* — the
  default 1.3 MB app partition overflows (~104%).
- **Flash Size:** `4MB`
- **CPU Frequency:** `160MHz`
- **USB CDC On Boot:** `Enabled` (only needed if you want `Serial` debug over USB-C)
- **Upload speed:** `921600`

## ⚠️ lv_conf.h placement (most common gotcha)

LVGL reads its config from a file **next to** the `lvgl` library folder, not
from this sketch folder. After installing LVGL:

```
<Arduino>/libraries/
    lv_conf.h        ← copy THIS repo's lv_conf.h here
    lvgl/            ← the installed library
    LovyanGFX/
```

Without it you'll get errors about missing fonts (`lv_font_montserrat_48`) or a
blank screen.

## Touch

Touch is **not** handled by LovyanGFX. The CST816 is driven directly over I²C
in `Display.cpp` so its **auto-sleep can be disabled** (register `0xFE = 0x01`)
— otherwise the controller dozes off after a few idle seconds and the next tap
is lost ("works once then dead"). The INT pin (GPIO0, a strap pin) is not used;
the driver polls over I²C. LovyanGFX handles the SPI display only.

## Configure your adapter

Pick the network on the on-screen **WiFi picker** at boot (your choice is saved
to flash). Defaults live in `config.h` if you'd rather hard-code them:

```c
#define OBD_WIFI_SSID  "WiFi_OBDII"   // check your adapter's label
#define OBD_WIFI_PASS  ""             // most V1.5 clones are open
#define OBD_HOST       "192.168.0.10"
#define OBD_PORT       35000
```

Other common SSIDs: `WiFi327`, `V-LINK`, `OBDII`. Some units use password
`12345678` (set `OBD_WIFI_PASS`). The adapter must be powered (plugged into the
car with ignition on, or a bench 12 V supply) for its SSID to appear in the scan.

## Testing without a car

Tap **Demo** on the WiFi picker to drive the whole UI with simulated data
(sweeping RPM/speed, warming coolant that crosses the overheat threshold, fake
DTCs). No adapter needed. Alternatively point it at a PC running
[ELM327-emulator](https://github.com/Ircama/ELM327-emulator)
(`elm -n 35000 -s car`).

## File map

| File | Responsibility |
|------|----------------|
| `esp32-wifi-obd.ino` | setup/loop wiring the modules together |
| `config.h` | pins, WiFi/OBD endpoint, ranges, timeouts |
| `Display.{h,cpp}` | LovyanGFX SPI display + direct CST816 I²C touch + LVGL glue |
| `ObdClient.{h,cpp}` | non-blocking ELM327 TCP state machine + PID/DTC parsing |
| `Ui.{h,cpp}` | LVGL screens, gauges, swipe/hold input, pickers, warnings |
| `Theme.{h,cpp}` | light/dark palette |
| `Settings.{h,cpp}` | persisted theme/units/rpmMax/warnTemp/brightness + conversions |
| `lv_conf.h` | LVGL build config (copy to `libraries/`) |

## Troubleshooting

- **Sketch too big (~104%):** set Partition Scheme to `Huge APP (3MB)`.
- **Blank / white screen:** `lv_conf.h` not in `libraries/`, or wrong LVGL major version (must be 8.x).
- **Colors inverted / wrong:** flip `c.invert` in `Display.cpp` (GC9A01 clones vary), or the swap flag in the flush callback.
- **Touch dead / breaks after idle:** should be fixed by the direct CST816 driver + auto-sleep off; confirm touch pins (SDA=4, SCL=5, RST=1, addr 0x15). If mirrored, flip X/Y in `touch_cb`.
- **Stuck on "Joining adapter WiFi...":** wrong SSID/password, or the adapter isn't powered (ignition on).
- **Values stay `--`:** vehicle protocol not detected — the first probe can take several seconds; a cheap clone may only support a few PIDs.
- **Garbled DTCs:** multi-frame CAN responses aren't fully reassembled; the parser handles the common single-frame case. Verify against a proper scanner before acting on codes.

## Notes & limits

- ELM327 clones are slow (~10–20 reads/s total); this polls 4 PIDs round-robin, plenty for a smooth gauge.
- No internet while joined to the adapter AP (single WiFi interface) — expected for standalone use.
- The overheat warning is shown on the Coolant screen only (kept off the global layer so it can never cover the controls).
- Power the board from a 12 V→5 V USB car adapter; the display draws ~150 mA.
