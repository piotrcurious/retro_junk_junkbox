#include <stdio.h>
#include <stdlib.h>
#include <pci/pci.h>

#define PIIX4_BUS 0
#define PIIX4_DEVICE 7
#define PIIX4_FUNCTION 0
#define ISA_CLK_DIV_REG 0x4C

int main() {
    struct pci_access *pacc;
    struct pci_dev *dev;
    uint8_t current_val;

    // Initialize the PCI library
    pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    // Find the PIIX4 device
    for (dev = pacc->devices; dev; dev = dev->next) {
        if (dev->bus == PIIX4_BUS && dev->dev == PIIX4_DEVICE && dev->func == PIIX4_FUNCTION) {
            printf("Found Intel PIIX4 (Bus: %02x, Device: %02x, Function: %02x)\n",
                   dev->bus, dev->dev, dev->func);

            // Read the current value of the ISA Clock Divider Register
            current_val = pci_read_byte(dev, ISA_CLK_DIV_REG);
            printf("Current value of register 0x%02X: 0x%02X\n", ISA_CLK_DIV_REG, current_val);

            // Check the current ISA frequency
            if (current_val & 0x01) {
                printf("Current ISA frequency is ~11.11MHz (Divide by 3)\n");
            } else {
                printf("Current ISA frequency is ~8.33MHz (Divide by 4)\n");
            }

            // Modify the value to set the divide-by-3 option
            uint8_t new_val = current_val | 0x01;
            printf("Attempting to set new value to: 0x%02X\n", new_val);
            
            // Write the new value back to the register
            pci_write_byte(dev, ISA_CLK_DIV_REG, new_val);

            // Read back to verify the change
            uint8_t verify_val = pci_read_byte(dev, ISA_CLK_DIV_REG);
            printf("Verified new value: 0x%02X\n", verify_val);

            if (verify_val & 0x01) {
                printf("ISA frequency successfully set to ~11.11MHz.\n");
            } else {
                printf("Failed to set ISA frequency.\n");
            }
            
            break; // Exit the loop after finding the device
        }
    }

    if (!dev) {
        printf("Intel PIIX4 device not found on Bus %02x, Device %02x, Function %02x. Aborting.\n",
               PIIX4_BUS, PIIX4_DEVICE, PIIX4_FUNCTION);
    }

    // Clean up the PCI library
    pci_cleanup(pacc);

    return 0;
}
