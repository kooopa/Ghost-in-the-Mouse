# Ghost-in-the-Mouse
STM32F103-based HID mouse emulator that records and replays authentic human movement via SPI Flash.


A simple DIY tool for keeping a PC active by replaying recorded mouse movements.

Instead of relying on random mathematical patterns to move the cursor, this device records your actual mouse input and saves it to an external SPI Flash chip. When in playback mode, it mimics your real-life movement style, making it look like someone is actually using the computer.
How it works

The device uses an STM32F103 and acts as a composite HID+CDC device.

    Recording Mode: Power the device with pin PB12 pulled to ground. Connect it to your PC and use the included Python script to capture your movement data, which is then stored in the SPI Flash memory.

    Playback Mode: Power the device with PB12 left floating (high). The device will immediately start replaying the captured movement data as a standard USB mouse.

Hardware

    MCU: STM32F103

    Storage: External SPI Flash chip (for storing movement logs)

    Interface: Dual-mode USB (HID + CDC)
