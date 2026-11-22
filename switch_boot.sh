#!/usr/bin/env bash
# Manage default dual-boot slot between MeshCore (slot 0) and ADS-B Mode S (slot 1)
# on Heltec WiFi LoRa 32 V3.
set -e

PORT="${PORT:-/dev/ttyUSB0}"
PYTHON="${PYTHON:-python3}"
IDF_DIR="${IDF_PATH:-$HOME/esp/esp-idf}"

export IDF_PATH="$IDF_DIR"
export PYTHONPATH="$IDF_DIR/components/partition_table:$IDF_DIR/components/app_update${PYTHONPATH:+:$PYTHONPATH}"

usage() {
    echo "Usage: $0 [adsb | meshcore | status]"
    echo ""
    echo "Commands:"
    echo "  adsb      Set default boot partition to ADS-B Mode S (slot 1)"
    echo "  meshcore  Set default boot partition to MeshCore (slot 0)"
    echo "  status    Display current OTA boot data"
    echo ""
    echo "Environment variables:"
    echo "  PORT      Serial port (default: /dev/ttyUSB0)"
    echo "  IDF_PATH  Path to ESP-IDF installation (default: \$HOME/esp/esp-idf)"
    echo "  PYTHON    Python interpreter with esptool installed (default: python3)"
    exit 1
}

CMD="$1"
if [ -z "$CMD" ]; then
    usage
fi

case "$CMD" in
    adsb)
        echo "Setting default boot target to ADS-B Mode S (slot 1) on $PORT..."
        "$PYTHON" "$IDF_DIR/components/app_update/otatool.py" -p "$PORT" switch_ota_partition --slot 1
        echo "Done. Next boot will load ADS-B Mode S."
        ;;
    meshcore)
        echo "Setting default boot target to MeshCore (slot 0) on $PORT..."
        "$PYTHON" "$IDF_DIR/components/app_update/otatool.py" -p "$PORT" switch_ota_partition --slot 0
        echo "Done. Next boot will load MeshCore."
        ;;
    status)
        echo "Reading OTA boot data from $PORT..."
        "$PYTHON" "$IDF_DIR/components/app_update/otatool.py" -p "$PORT" read_otadata
        ;;
    *)
        usage
        ;;
esac
