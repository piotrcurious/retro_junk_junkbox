#!/bin/bash

# Device address in sysfs format (Domain:Bus:Device.Function)
PCI_DEV="0000:00:07.0"
CONFIG_PATH="/sys/bus/pci/devices/$PCI_DEV/config"
REG_OFFSET=0x4C

# Ensure running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (or with sudo)"
   exit 1
fi

# Check if the PCI device exists
if [ ! -e "$CONFIG_PATH" ]; then
    echo "Error: PCI device $PCI_DEV not found at $CONFIG_PATH"
    exit 1
fi

# Read one byte at offset 0x4C
current_val=$(xxd -ps -s $REG_OFFSET -l1 "$CONFIG_PATH")
current_val=$((0x$current_val))

echo "Current value at 0x4C: 0x$(printf '%02X' $current_val)"

if (( current_val & 0x01 )); then
    echo "Current ISA frequency: ~11.11 MHz (Divide by 3)"
else
    echo "Current ISA frequency: ~8.33 MHz (Divide by 4)"
fi

# Set bit 0 to 1 to select divide-by-3
new_val=$(( current_val | 0x01 ))
echo "Setting new value to 0x$(printf '%02X' $new_val)..."

# Use printf to construct the byte and dd to write
printf "\\x%02x" $new_val | dd of="$CONFIG_PATH" bs=1 seek=$((0x4C)) count=1 conv=notrunc 2>/dev/null

# Read back to verify
verify_val=$(xxd -ps -s $REG_OFFSET -l1 "$CONFIG_PATH")
verify_val=$((0x$verify_val))

echo "Verified value: 0x$(printf '%02X' $verify_val)"
if (( verify_val & 0x01 )); then
    echo "ISA frequency successfully set to ~11.11 MHz."
else
    echo "Failed to set ISA frequency."
fi
