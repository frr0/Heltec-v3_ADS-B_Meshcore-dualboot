# Heltec-v3_ADS-B_Meshcore-dualboot

Dual-boot firmware setup for the **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262, 8 MB flash) combining a **1090 MHz Mode S / ADS-B receiver** and **MeshCore Companion Radio BLE** on the same board, with hardware button switching and isolated persistent storage.

Forked from [antirez/sx1262-adsb](https://github.com/antirez/sx1262-adsb).

Android companion app for the ADS-B receiver: [https://github.com/frr0/adsb-companion](https://github.com/frr0/adsb-companion).

---

## Summary of Changes

- **Heltec WiFi LoRa 32 V3 Hardware Port**
  - Adapted SX1262 SPI bus mapping (`CS=8, SCK=9, MOSI=10, MISO=11, RST=12, BUSY=13`), 1.8 V TCXO on DIO3, and DIO2 RF switch control.
  - Adapted SSD1306 128x64 OLED (`SDA=17, SCL=18`, `RST=21`, active-LOW `Vext` power rail on `GPIO 36`).
  - Configured ESP32-S3FN8 build defaults (8 MB flash, no SPIRAM, UART console over CP2102).
- **BLE NUS Interface and Android Companion Support**
  - Added Nordic UART Service (NUS) over NimBLE (`Heltec-ADSB`) for real-time OLED mirroring (`SXSCREEN`), live packet stream control, remote PRG button emulation, frequency tuning, and flash log dump.
  - Assigned a dedicated static random BLE address to ADS-B so Android never collides with MeshCore's bonded public Bluetooth MAC address.
- **Distinct-Aircraft History Navigation**
  - Single-tapping PRG jumps directly to the previous distinct ICAO aircraft (`history_prev_aircraft`), skipping redundant burst frames; long-pressing returns to live view.
- **Hardware Dual-Boot and Storage Isolation**
  - Built a custom **ESP-IDF v4.4.7 QIO dual-boot bootloader** (`firmware/bootloader_dualboot_idf44.bin`) with a 2.5 s boot selection window indicated by the onboard White LED (`GPIO 35`) and triggered via the `PRG` button (`GPIO 0`).
  - Using ESP-IDF v4.4.7 with QIO flash support (`XM25QU64A`) ensures full SPIFFS persistence for MeshCore (built on ESP-IDF v4.4.7) while remaining forward-compatible with the ESP-IDF v5.2.x ADS-B firmware.
  - Isolated ADS-B storage onto a dedicated `captures` partition (`0x540000`, subtype `0x99`) so MeshCore retains exclusive access to its stock `spiffs` partition (`0x670000`, 1.5 MB) for cryptographic identity keys, contacts, and settings.

---

## Flash Partition Layout (8 MB)

| Offset | Size | Partition | Subtype | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| `0x000000` | 32 KB | `bootloader` | - | ESP-IDF v4.4.7 QIO dual-boot bootloader |
| `0x008000` | 4 KB | `partition-table` | - | Custom dual-boot partition table |
| `0x009000` | 20 KB | `nvs` | `nvs` | BLE bonds and persistent radio settings |
| `0x00e000` | 8 KB | `otadata` | `ota` | Default boot slot selection |
| `0x010000` | 3.2 MB | `app0` | `ota_0` | MeshCore Companion BLE (slot 0, default) |
| `0x340000` | 2.0 MB | `app1` | `ota_1` | ADS-B Mode S Receiver (slot 1) |
| `0x540000` | 1.2 MB | `captures` | `0x99` | Dedicated SPIFFS storage for ADS-B logs |
| `0x670000` | 1.5 MB | `spiffs` | `spiffs` | Dedicated stock SPIFFS storage for MeshCore |
| `0x7f0000` | 64 KB | `coredump` | `coredump` | Crash dump partition |

---

## Switching Between MeshCore and ADS-B

### Hardware Button (No PC Required)
1. Power on the board or press **RST**.
2. The onboard **White LED** lights up for **2.5 seconds**:
   - Do nothing to boot the default firmware (**MeshCore**).
   - Tap **PRG** while the LED is lit to boot the alternate firmware (**ADS-B Mode S**).
3. Press **RST** again without touching PRG to return to the default firmware.

Note: Do not hold PRG while pressing RST, as GPIO 0 is the ESP32-S3 ROM download strap pin. Press PRG only after the White LED turns on.

### Software Switch (Over USB)
Change the persistent default boot slot from a host machine:

```bash
./switch_boot.sh meshcore
./switch_boot.sh adsb
./switch_boot.sh status
```

---

## Build and Flash

### Prerequisites
- ESP-IDF v5.2.x (`. $HOME/esp/esp-idf/export.sh`)
- Python 3 with `esptool`
- Standalone MeshCore Companion BLE binary for Heltec V3 (not the `-merged.bin` image) from [flasher.meshcore.io](https://flasher.meshcore.io)

### 1. Build ADS-B Firmware
```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py build
```

### 2. Initial Dual-Boot Flash
```bash
./flash_dualboot.sh /path/to/Heltec_v3_companion_radio_ble.bin
```

### 3. Updating Individual Firmwares Later
- **Update ADS-B only** (leaves MeshCore, bootloader, and SPIFFS untouched):
  ```bash
  python3 -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 921600 write_flash 0x340000 build/lilygo_adsb.bin
  ```
- **Update MeshCore only** (leaves ADS-B, bootloader, and SPIFFS untouched):
  ```bash
  python3 -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 921600 write_flash 0x10000 /path/to/Heltec_v3_companion_radio_ble.bin
  ```

---

## Companion App

The Android companion application for the ADS-B receiver is maintained at [https://github.com/frr0/adsb-companion](https://github.com/frr0/adsb-companion). It connects via BLE NUS (`Heltec-ADSB`) and provides live OLED mirroring, decoded flight telemetry, remote button control, frequency tuning, and flash capture export.

---

## License and Credits

- Original SX1262 ADS-B demodulator and Mode S decoder: [antirez/sx1262-adsb](https://github.com/antirez/sx1262-adsb) (see [LICENSE](LICENSE)).
- Companion LoRa mesh firmware: [MeshCore](https://github.com/meshcore-dev/MeshCore).
