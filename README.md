# Halo

A 2.1" round smart clock, built on an ESP32-C3 Zero and a 360×360 GC9B72 TFT. It started as a classroom clock and now runs at home as a weather clock.

- **Weather face:** time, temperature, conditions, high and low, and the next 12 hours of rain chance around the rim.
- **Night dimming:** the backlight drops to a low level on a schedule.
- **Web dashboard:** at `http://halo.local`, with a JSON API for tools such as Home Assistant.
- **Self-update:** the dashboard's Update button installs the latest GitHub release.

Weather data by [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0).

## First flash, by USB

Self-update needs a different flash layout, so one USB upload is required.

1. Optional: copy `HaloClock/secrets.example.h` to `HaloClock/secrets.h` and fill in your Wi-Fi. Without it, the clock keeps the Wi-Fi it already has saved, or starts the `Halo-Clock` hotspot.
2. In the Arduino IDE, open `HaloClock/HaloClock.ino` and choose **ESP32C3 Dev Module**, USB CDC On Boot: **Enabled**, Partition Scheme: **Minimal SPIFFS**.
3. Upload, then press **RESET** on the board.

Clock settings reset to defaults on this upload. Wi-Fi settings are kept.

## Releasing an update

1. Raise `FIRMWARE_VERSION` in `HaloClock/HaloClock.ino` and push to `main`.
2. Tag it, using the same version: `git tag v1.3.1 && git push origin v1.3.1`.
3. GitHub Actions runs the simulator, compiles and publishes `halo-firmware.bin` to a release.
4. On the dashboard, open **Firmware**, choose **Check for updates**, then **Update now**.

New firmware runs on trial. If it crashes, or can't get online within 3 minutes, the clock returns to the previous version. A change to `Settings` in `model.h` needs `kSettingsVersion` raised and resets clock settings when installed.

## Simulator

`tools/sim/run.sh` runs the real faces on a Mac and checks for drawing artifacts. Images go to `tools/sim/out/`. Use `tools/sim/run.sh weather` to run only matching scenarios.

## API

POST bodies are form-encoded.

| Route | Purpose |
|---|---|
| `GET /api/state` | Settings, network, weather and update status |
| `GET /api/weather` | Weather only |
| `POST /api/weather` | Fetch new weather now |
| `POST /api/set` | Change settings: `face`, `brightness`, `lat` with `lon`, `place`, `nightOn`, `nightStart`, `nightEnd`, `nightBrightness` and more |
| `POST /api/update` | `a=check` or `a=install` |
| `GET /api/info` | Diagnostics |

The API has no login. Keep it on a trusted home network and don't expose it to the internet.
