# Halo

A 2.1" round smart clock, built on an ESP32-C3 Zero and a 360×360 GC9B72 TFT. It started as a classroom clock and is now a single-purpose home weather clock.

- **Weather face:** time, temperature, conditions, high and low, and the next 12 hours of rain chance around the rim.
- **Night dimming:** the backlight drops to a low level on a schedule.
- **Web dashboard:** at `http://halo.local`, with a JSON API for tools such as Home Assistant.
- **Self-update:** the dashboard's Update button installs the latest GitHub release.
- **Home Assistant:** reads and controls the clock, and shows the alarm state and short messages on the face.

Weather data by [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0).

## First flash, by USB

Self-update needs a different flash layout, so one USB upload is required.

1. Optional: copy `HaloClock/secrets.example.h` to `HaloClock/secrets.h` and fill in your Wi-Fi. Without it, the clock keeps the Wi-Fi it already has saved, or starts the `Halo-Clock` hotspot.
2. In the Arduino IDE, open `HaloClock/HaloClock.ino` and choose **ESP32C3 Dev Module**, USB CDC On Boot: **Enabled**, Partition Scheme: **Minimal SPIFFS**.
3. Upload, then press **RESET** on the board.

Clock settings reset to defaults on this upload. Wi-Fi settings are kept.

## Building on a Mac

`tools/build.sh` compiles into `build/` with its own copy of the libraries, kept in `~/Library/Application Support/halo-arduino`. It avoids the Arduino IDE's library folder because cloud-synced folders such as iCloud Documents can leave `file 2.cpp` conflict copies there, which break linking. `tools/build.sh upload /dev/cu.usbmodemXXXX` also flashes over USB.

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
| `POST /api/set` | Change settings: `brightness`, `home`, `lat` with `lon`, `place`, `nightOn`, `nightStart`, `nightEnd`, `nightBrightness`, `testOffset`, `deviceName` |
| `POST /api/update` | `a=check` or `a=install` |
| `POST /api/ha` | From Home Assistant: `alarm` (an alarm panel state such as `armed_away` or `triggered`) and/or `notice` (short text, empty to clear), each shown for `ttl` seconds, 900 by default |
| `GET /api/info` | Diagnostics |

## Home Assistant

Home Assistant reaches the clock at `http://halo.local`; its HTTP client resolves `.local` names itself, so the clock's DHCP address can change freely. The package lives in the Home Assistant project (`ha/packages/halo_clock.yaml`). It polls `/api/state`, offers brightness, night dimming, weather refresh and update controls, and sends the alarm state every 5 minutes and on every change.

On the face, a lock above the date means the alarm is armed (green), arming or in entry delay (peach) or triggered (coral). Entry delay and triggered also replace the conditions line. A notice replaces the bottom line. Everything from Home Assistant expires on its own, so if Home Assistant stops, the face clears within 15 minutes.

The API has no login. Keep it on a trusted home network and don't expose it to the internet.
