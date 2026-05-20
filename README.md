# ESP32-C3 Supermini 0.42" OLED Demo

Arduino demo sketch for an ESP32-C3 Supermini driving a 0.42" SSD1306 OLED panel.

The sketch renders a looping demo with:

- scrolling text
- a lightweight Space Invaders-style scene
- a Pac-Man chase animation
- a calibration outline and fill pass for the active display window

## Hardware

- ESP32-C3 board with I2C OLED support
- 0.42" OLED panel using an SSD1306-compatible controller
- Tested for the common 128x64 framebuffer layout with a calibrated visible window of 72x40 pixels

The sketch uses these I2C pins:

- SDA: `GPIO 5`
- SCL: `GPIO 6`

It probes OLED addresses `0x3C` and `0x3D`.

## Dependencies

Install these Arduino libraries:

- `Adafruit GFX Library`
- `Adafruit SSD1306`

## What The Sketch Does

The animation runs in phases:

1. Scrolling banner text
2. Autoplay Space Invaders-inspired scene
3. Pac-Man chase from right to left
4. Pac-Man chase from left to right
5. Calibration hold
6. Fill bar sweep across the active OLED window

The display window is calibrated in code with:

- `ACTIVE_X = 28`
- `ACTIVE_Y = 24`
- `ACTIVE_W = 72`
- `ACTIVE_H = 40`

## Tuning

Useful constants are defined near the top of the sketch:

- `CALIBRATION_ONLY_MODE` to show only the display outline
- `SHOW_CALIBRATION_GUIDE` to overlay the outline on every frame
- `FRAME_MS` to change the frame rate
- `ACTIVE_*` values to adjust the visible OLED window

## Troubleshooting

- If the OLED does not initialize, the sketch scans the I2C bus and waits forever after reporting what it found.
- If the panel appears shifted or clipped, adjust the `ACTIVE_*` calibration values.
- If uploads fail, confirm the board profile, serial port, and USB CDC settings match the command above.

## License

MIT. See [`LICENSE`](./LICENSE).
