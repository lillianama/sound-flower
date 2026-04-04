# Sound Flower

An audio-reactive LED art piece that listens to the world around it and blooms with light. Sound Flower captures live audio through a tiny microphone, transforms it into frequency data with real-time FFT, and paints the results across two LED displays -- a vibrant 16x16 RGB matrix and a charming little 8x8 shift-register grid. Every beat, every note, every whisper of sound becomes a garden of color.

Built on the Arduino Nano ESP32 with the ESP-IDF framework, because beautiful things deserve a solid foundation.

## Features

- **Real-time audio spectrum analysis** -- 1024-sample FFT at 48 kHz via the ESP-DSP library, capturing frequencies from deep bass to sparkling highs
- **16x16 WS2812 RGB matrix** -- 256 individually-addressable LEDs display 8 frequency bands as rainbow bar graphs with smooth peak-hold indicators
- **8x8 shift register matrix** -- a second display driven by dual 74HC595 shift registers with hardware-multiplexed refresh on a dedicated CPU core
- **Dual-core architecture** -- audio processing and the main visualization loop run on core 0, while the shift register display refreshes at 20 kHz on core 1
- **DMA-based audio capture** -- continuous ADC sampling with interrupt-driven processing, so not a single sample is missed
- **Graceful animations** -- band smoothing (75 ms hold), peak hold (500 ms), and gentle peak decay make the visualizations feel alive and organic

## How It Works

```
Microphone -> ADC (48 kHz, 12-bit) -> FFT (1024 samples) -> 8 Frequency Bands -> LED Visualizations
```

The audio signal is continuously sampled into a DMA buffer. When a frame is ready, it's converted to floating-point, run through a real-valued FFT, and split into 8 perceptually-weighted frequency bands spanning sub-bass through brilliance. Each band drives a column on the RGB matrix (rainbow gradient from bottom to top, white peak pixel) and a row on the 8x8 grid.

## Hardware

| Component | Details |
|---|---|
| Microcontroller | Arduino Nano ESP32 (ESP32-S3, 16 MB flash, 8 MB PSRAM) |
| RGB LED Matrix | WS2812 16x16 (256 LEDs), serpentine layout |
| Mono LED Matrix | 8x8 with 2x 74HC595 shift registers |
| Microphone | Analog mic on ADC1 Channel 0 |

### Pin Configuration

| Pin | Function |
|---|---|
| GPIO 18 | WS2812 data line |
| GPIO 5 | Shift register data (DS) |
| GPIO 6 | Shift register clock (SH_CP) |
| GPIO 7 | Shift register latch (ST_CP) |
| ADC1_CH0 | Microphone input |

## Getting Started

### Prerequisites

1. **ESP-IDF v5.1.4** -- install via the [ESP-IDF VS Code extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension). You must use v5.1.4 specifically, as Arduino Core for ESP32 requires it.

2. During extension setup, select **Express** and set the version to **v5.1.4**. The SDK is about 1.4 GB.

3. **macOS note:** the extension sometimes fails to install GCC toolchains. If `~/.espressif/tools/xtensa-esp32s3-elf` doesn't exist after installation, run `~/esp/esp-idf/install.sh` in a terminal.

### Preparing Your Arduino Nano ESP32

The Nano ESP32 ships with Arduino's bootloader, which ESP-IDF can't use. To switch to ROM boot mode:

1. Connect the board via USB
2. Bridge pin **B1** to **GND** with a jumper wire -- the LED turns green
3. While the LED is green, press the **reset** button
4. Remove the jumper -- the LED turns purple

Done! The first time you flash, you'll need to press reset once more after flashing. After that, it's automatic.

To restore the original Arduino bootloader later, follow [Arduino's guide](https://support.arduino.cc/hc/en-us/articles/9810414060188-Reset-the-Arduino-bootloader-on-the-Nano-ESP32).

### Clone

```bash
git clone --recurse-submodules https://github.com/lillianama/sound-flower
```

This project uses a [custom fork of FastLED](https://github.com/lillianama/FastLED/tree/lillianama/esp-idf_arduino_nano_esp32) as a git submodule.

### Build & Flash

Open the project in VS Code with the ESP-IDF extension installed, then:

| Action | Shortcut |
|---|---|
| Build & flash | `Cmd+Shift+B` (macOS) / `Ctrl+Shift+B` |
| Serial monitor | Command palette -> `ESP-IDF: Monitor your device` |
| Debug | `F5` (uses built-in JTAG, starts OpenOCD automatically) |

Or from the command line:

```bash
# Build
idf.py build

# Flash
idf.py -p /dev/tty.usbmodem1201 -b 115200 flash

# Monitor serial output
idf.py -p /dev/tty.usbmodem1201 monitor
```

### Tips

- Use `printf()` instead of `Serial.println()` -- the built-in JTAG/Serial interface doesn't play well with Arduino's Serial library
- The ESP32-S3 only has **2 hardware breakpoints** -- keep it to 1-2 for comfortable debugging
- UART flashing is faster than JTAG/OpenOCD flashing, so the default build task uses UART
- Code compiles with `-Og` by default, which can make stepping look a little odd but keeps the binary small enough to fit in flash

## Frequency Bands

| Band | Range | What You'll Hear |
|---|---|---|
| 0 | 0 -- 62 Hz | Sub-bass, the rumble |
| 1 | 63 -- 125 Hz | Bass, kick drums |
| 2 | 125 -- 250 Hz | Low mids, warmth |
| 3 | 250 -- 500 Hz | Midrange, body |
| 4 | 500 Hz -- 1 kHz | Upper mids, presence |
| 5 | 1 -- 2 kHz | Clarity, vocals |
| 6 | 2 -- 4 kHz | Brilliance, detail |
| 7 | 4 -- 12 kHz | Air, sparkle |

## Dependencies

- [ESP-IDF v5.1.4](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32/index.html)
- [Arduino Core for ESP32 v3.0.4](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [FastLED](https://github.com/lillianama/FastLED/tree/lillianama/esp-idf_arduino_nano_esp32) (custom fork for ESP-IDF + Nano ESP32 compatibility)
- [ESP-DSP](https://github.com/espressif/esp-dsp) (FFT processing)
