# MeshCore E22P-868M30S Repeater

MeshCore repeater firmware for the **Seeed Studio XIAO ESP32S3** and the
**[Ebyte E22P-868M30S](https://www.cdebyte.com/products/E22P-868M30S)** LoRa
module.

This is a standalone, single-target firmware project based on MeshCore repeater
v1.18.0. It supports only the XIAO ESP32S3 + E22P-868M30S combination and adds
radio diagnostics, optional home Wi-Fi and protected LAN OTA.

## Highlights

- Dedicated `Xiao_S3_E22P_repeater` PlatformIO environment.
- Full E22P output using SX1262 power `22 dBm` and the module's RF front end.
- DIO2 RF switch control, 1.8 V DIO3 TCXO and E22P EN control.
- Periodic AGC reset disabled by default (`agc.reset.interval=0`) for reliable RX.
- Upstream noise-floor sampling; with `int.thresh=0` it does not gate TX.
- `health`, `diag`, `queue`, `tx sched`, `radio diag`, `dense`, `cli stats` and `tx diag` commands.
- Original temporary `MeshCore-OTA` access point through `start ota`.
- Optional persistent home Wi-Fi through WiFiManager and `wifi on`.
- Password-protected LAN OTA, diagnostics and HTTP CLI.

### Receive stability

Periodic four-second AGC resets caused dropped packets and command timeouts in
field testing. This release disables them by default and retains upstream
noise-floor sampling. With the default `int.thresh=0`, noise-floor telemetry
does not gate transmission. The noise-floor value reported with this radio
module is inaccurate and must not be treated as a reliable measurement of the
RF environment. Use `radio diag`, `queue` and `tx sched` to check
RX/TX completion, queue drainage and radio start failures.

The upstream FAQ suggests a four-second reset as a generic SX1262 deafness
workaround. That advice predates v1.18.0; the new release fixes interval
validation, while upstream still defaults to zero. For this E22P build, field
tests support keeping periodic resets disabled.

Upgrading preserves the saved configuration. If you used an older release with
periodic resets enabled, set `agc.reset.interval` to `0` after upgrading:

```text
set agc.reset.interval 0
```

### Health interpretation

`health` is an advisory summary, not a hardware fault detector. Since
`1.18.0-e22p.2`, `rxerr` is failed reads divided by all read attempts
(successful + failed), using counters since boot or `clear stats`.

- RX errors below 30% alone do not change `good` to a warning.
- RX errors at or above 30% produce `warn`, never `bad` by themselves.
- At least 50% flood duplicates or 8 queued outbound packets produce `warn`.
- Disabled forwarding or at least 16 queued outbound packets produce `bad`.

These are local diagnostic thresholds, not universal acceptable packet-loss
limits. Check actual delivery, `radio diag` and queue drainage when investigating
problems. `good` does not prove every link is reliable. Noise floor is displayed
for reference and never affects this rating.

## Hardware

The E22P requires a stable external supply. For full output, use approximately
5 V with enough capacity for 600-670 mA TX peaks. Do not power it from the XIAO
3V3 pin. All grounds must be connected together.

| E22P | XIAO ESP32S3 | GPIO |
| --- | --- | ---: |
| SCK | D8 | 7 |
| MISO | D9 | 8 |
| MOSI | D10 | 9 |
| NSS | D4 | 5 |
| DIO1 | D1 | 2 |
| BUSY | D3 | 4 |
| NRST | D2 | 3 |
| EN | D5 / SCL | 6 |
| T/R CTRL | SX1262 DIO2 | - |

See [docs/wiring.md](docs/wiring.md) before connecting or powering the module.

## Build

### Ready-to-flash firmware

Most users do not need PlatformIO. Download both files from the
[latest GitHub release](https://github.com/Sukecz/MeshCore-E22P-Repeater/releases/latest):

- `MeshCore-E22P-868M30S-merged.bin` — first installation over USB with the
  MeshCore Web Flasher;
- `MeshCore-E22P-868M30S.bin` — later OTA updates only.

#### First installation with MeshCore Web Flasher

1. Connect the XIAO ESP32S3 to your computer over USB.
2. Open the official [MeshCore Web Flasher](https://meshcore.io/flasher) in a
   Web Serial compatible browser such as Chrome or Edge.
3. Choose **Custom Firmware**.
4. Select `MeshCore-E22P-868M30S-merged.bin` from the GitHub release.
5. Select the XIAO serial port and start flashing.
6. After reboot, open the flasher's serial console at 115200 baud to configure
   the repeater name, admin password and radio settings.

The merged image includes the ESP32-S3 bootloader, partition table and
application and is flashed at offset `0x0`. Do not use the non-merged OTA image
for a first installation.

### Build from source

Install [PlatformIO](https://platformio.org/), clone this repository and run:

```bash
pio run -e Xiao_S3_E22P_repeater
```

Application image for OTA:

```text
.pio/build/Xiao_S3_E22P_repeater/firmware.bin
```

Create the merged image for the first USB/web-flasher installation:

```bash
pio run -e Xiao_S3_E22P_repeater -t mergebin
```

Flash `firmware-merged.bin` at offset `0x0`. Use the normal `firmware.bin` for
later OTA updates.

## OTA and optional Wi-Fi

Wi-Fi is **off by default** on a fresh installation.

- `start ota` starts the original temporary `MeshCore-OTA` access point for
  10 minutes.
- `stop ota` stops the temporary OTA access point.
- `wifi on` enables persistent home Wi-Fi. WiFiManager opens
  `MeshCore-WiFi-<node-name>` only when saved credentials are missing or fail.
- `wifi off` disables persistent Wi-Fi and the Wi-Fi radio.

For protected LAN OTA and HTTP tools, configure one password from an
authenticated MeshCore repeater console:

```text
set web.password <at-least-8-characters>
web on
wifi on
```

The fixed web username is `admin`. The password protects `/update`, `/cmd`,
`/diag`, `/radio` and `/log`. Use the web interface only on a trusted local
network; it uses HTTP Basic Authentication and must not be exposed to the
internet.

When upgrading an older build that already has persistent STA enabled, the
existing MeshCore admin password is used as a migration fallback until a
separate web password is saved. Fresh installations do not enable this fallback.

See [docs/e22p_commands.md](docs/e22p_commands.md) for the command reference.

## Radio defaults

- Frequency: 869.618 MHz
- Bandwidth: 62.5 kHz
- Spreading factor: 8
- Coding rate: 5
- SX1262 TX power: 22 dBm
- SX1262 current limit: 140 mA
- E22P AGC reset interval: 0 seconds (disabled)

The 140 mA value is the SX1262 current limit, not the total E22P module supply
current. Verify that your frequency, antenna, output power and duty cycle comply
with local regulations.

## Credits and license

Based on [MeshCore](https://github.com/meshcore-dev/MeshCore) repeater v1.18.0.
The source remains available under the MIT license in [license.txt](license.txt).
This project is not an official Ebyte or MeshCore product.
