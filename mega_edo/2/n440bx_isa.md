To increase the ISA slot frequency on an Intel N440BX dual CPU motherboard using assembly code, you'll need to modify the chipset configuration registers that control the ISA bus clock divider. Here's how to approach this:

## Understanding the N440BX ISA Clock

The N440BX chipset derives the ISA bus clock from the PCI clock (33MHz) through a divider. By default:
- ISA clock = PCI clock / 4 = 33MHz / 4 = 8.33MHz
- Some systems allow ISA clock = PCI clock / 3 = 33MHz / 3 = 11.11MHz

## Assembly Code Approach

You'll need to access the PCI configuration space of the PIIX4 South Bridge (device 07h, function 00h):

```assembly
; ISA frequency control via PIIX4 configuration
; Register offset 4Ch controls ISA clock divider

section .text
org 100h

start:
    ; Save current state
    pushf
    cli
    
    ; Read current PIIX4 configuration
    mov eax, 80003800h + 4Ch    ; Bus 0, Device 7, Function 0, Register 4Ch
    mov dx, 0CF8h               ; PCI Configuration Address Port
    out dx, eax
    
    mov dx, 0CFCh               ; PCI Configuration Data Port
    in al, dx
    
    ; Modify ISA clock divider bits
    ; Bit 0: ISA clock select (0=/4, 1=/3)
    or al, 01h                  ; Set to divide by 3 for ~11.11MHz
    
    ; Write back the modified value
    out dx, al
    
    ; Restore interrupts
    popf
    
    ; Exit to DOS
    mov ax, 4C00h
    int 21h
```

## Alternative Method Using Port I/O

Some N440BX systems support ISA frequency control through Super I/O ports:

```assembly
; Alternative method using Super I/O configuration
start:
    ; Enter Super I/O configuration mode
    mov dx, 2Eh                 ; Super I/O index port
    mov al, 87h
    out dx, al
    out dx, al                  ; Write twice to enter config mode
    
    ; Select ISA clock configuration register
    mov al, 24h                 ; ISA clock control register
    out dx, al
    
    inc dx                      ; Data port (2Fh)
    in al, dx                   ; Read current value
    
    ; Modify clock divider (varies by Super I/O chip)
    or al, 02h                  ; Enable higher ISA frequency
    out dx, al
    
    ; Exit configuration mode
    dec dx                      ; Back to index port
    mov al, AAh                 ; Exit configuration
    out dx, al
```

## Important Considerations

**⚠️ Warning**: Increasing ISA frequency can cause:
- ISA card instability or failure
- Data corruption on ISA devices
- System crashes or boot failures

**Safety measures**:
1. Test with non-critical ISA cards first
2. Have a recovery method (jumper reset, BIOS recovery)
3. Check ISA card specifications for maximum supported frequencies
4. Monitor system stability extensively

## Detection Code

Before modification, detect the current configuration:

```assembly
detect_isa_freq:
    ; Read PIIX4 register 4Ch
    mov eax, 80003800h + 4Ch
    mov dx, 0CF8h
    out dx, eax
    mov dx, 0CFCh
    in al, dx
    
    test al, 01h
    jz div_by_4
    ; ISA running at ~11.11MHz
    jmp freq_detected
    
div_by_4:
    ; ISA running at ~8.33MHz
    
freq_detected:
    ret
```

## Compilation and Usage

Assemble with NASM or similar:
```bash
nasm -f bin isa_freq.asm -o isa_freq.com
```

Run from DOS prompt with administrative privileges.

Remember that this modification affects hardware at a low level and should only be attempted if you understand the risks and have appropriate recovery methods available.
