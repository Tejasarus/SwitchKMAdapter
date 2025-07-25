# SwitchKMAdapter
Turns a Raspberry Pi Pico W into a Bluetooth keyboard and mouse adapter for the original Nintendo Switch and Nintendo Switch 2.

[List of known supported keyboards](https://bluepad32.readthedocs.io/en/latest/supported_keyboards/)

[List of known supported mice](https://bluepad32.readthedocs.io/en/latest/supported_mice/)

**NOTE:** If your keyboard/mouse is not listed on the supported list it may or may not work, try another device if yours doesn't work.

## Installing
1. Download latest `SwitchKMAdapter.uf2` file from [releases](https://github.com/Tejasarus/SwitchKMAdapter/releases).
2. Plug Pico W on PC while holding the bootsel button.
3. Drag and drop `SwitchKMAdapter.uf2` inside the Pico W root folder
4. Plug Pico W into Switch
5. Put both keyboard/mouse into pairing mode, they will auto pair to the Pico W

## Setup, Building, and Modifying
### What you need
1. A Raspberry Pi Pico W (Pico W 2 has not been tested)
2. CMake (3.13+) & GCC cross compiler
3. A way to connect it to the Switch/Dock (microUSB cable, type C dongle, etc.)

### Building
1. `git clone` this repo
2. `cd` into root of this repo
3. Run `git submodule update --init --recursive` to download all submodules
4. `cmake -G "MinGW Makefiles"`
5. `cmake --build .`
6. `SwitchKMAdapter.uf2` should generate inside the root of the project

### Modifying
To change which keys/mouse buttons are mapped to the switch buttons, you will need to modify the `pico_switch_platform.c` file located in the `\src` folder.

The functions `fill_gamepad_report_from_keyboard` and `fill_gamepad_report_from_mouse` contains the logic for mapping the keyboard/mouse to the switch.

For the list of keyboard keys refer to the `KeyboardKeys.h` file in the `\include` folder.

## Acknowledgements
- This project is a modified version of [PicoSwitch-WirelessGamepadAdapter](https://github.com/juan518munoz/PicoSwitch-WirelessGamepadAdapter) by [juan518munoz](https://github.com/juan518munoz) to work with a keyboard and mouse.
- [Bluepad32](https://github.com/ricardoquesada/bluepad32) by [ricardoquesada](https://github.com/ricardoquesada) 
- [TinyUSB](https://github.com/hathach/tinyusb) by [hathach](https://github.com/hathach)
