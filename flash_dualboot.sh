#!/usr/bin/env bash
# Flash the complete dual-boot layout (ESP-IDF 4.4.7 QIO bootloader, partition table,
# MeshCore companion BLE firmware, and ADS-B Mode S receiver) to Heltec WiFi LoRa 32 V3.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-/dev/ttyUSB0}"
PYTHON="${PYTHON:-python3}"
MESH_BIN="${1:-$MESH_BIN}"
BOOTLOADER_BIN="$SCRIPT_DIR/firmware/bootloader_dualboot_idf44.bin"
PART_TABLE_BIN="$SCRIPT_DIR/build/partition_table/partition-table.bin"
ADSB_BIN="$SCRIPT_DIR/build/lilygo_adsb.bin"

if [ -z "$MESH_BIN" ] || [ ! -f "$MESH_BIN" ]; then
    echo "Usage: $0 <path-to-meshcore-app.bin>"
    echo ""
    echo "Arguments:"
    echo "  path-to-meshcore-app.bin  Standalone MeshCore companion BLE binary (not -merged.bin)"
    echo ""
    echo "Environment variables:"
    echo "  PORT      Serial port (default: /dev/ttyUSB0)"
    echo "  PYTHON    Python interpreter with esptool installed (default: python3)"
    exit 1
fi

if [ ! -e "$PORT" ]; then
    echo "Error: Serial port $PORT not found. Connect the Heltec V3 board via USB."
    exit 1
fi

for f in "$BOOTLOADER_BIN" "$PART_TABLE_BIN" "$ADSB_BIN"; do
    if [ ! -f "$f" ]; then
        echo "Error: Required binary not found: $f"
        echo "Build the ADS-B firmware first with 'idf.py build'."
        exit 1
    fi
done

echo "1. Erasing NVS (BLE bonds) and SPIFFS for a clean initial state..."
"$PYTHON" -m esptool --chip esp32s3 -p "$PORT" -b 921600 erase_region 0x9000 0x5000
"$PYTHON" -m esptool --chip esp32s3 -p "$PORT" -b 921600 erase_region 0x670000 0x180000

echo "2. Flashing ESP-IDF 4.4.7 QIO dual-boot bootloader, partition table, MeshCore, and ADS-B..."
"$PYTHON" -m esptool --chip esp32s3 -p "$PORT" -b 921600 write_flash \
    0x0 "$BOOTLOADER_BIN" \
    0x8000 "$PART_TABLE_BIN" \
    0x10000 "$MESH_BIN" \
    0x340000 "$ADSB_BIN"

echo "3. Setting default boot partition to MeshCore (slot 0)..."
"$SCRIPT_DIR/switch_boot.sh" meshcore

echo "Dual-boot flashing complete."
echo "  Bootloader : ESP-IDF 4.4.7 QIO (2.5s White LED + PRG button switch)"
echo "  Slot 0     : MeshCore (0x10000, dedicated SPIFFS at 0x670000)"
echo "  Slot 1     : ADS-B Mode S (0x340000, dedicated captures partition at 0x540000)"
