# Desktop simulator

Runs the real firmware (`src/main.cpp`, all of `src/Ecran/*`, settings, WiFi setup flow, ...) on
macOS/Linux with an SDL window standing in for the 320x480 touch panel. No hardware needed.

```
brew install sdl2        # once
pio run                  # once, so PlatformIO downloads the libraries
cd sim && make run
```

| Input | Effect |
|---|---|
| mouse click / drag | touch / swipe |
| Up / Down | glucose +10 / -10 mg/dL |
| Home | reset glucose offset |
| S | save `screenshot-N.bmp` |
| R | restart (like `ESP.restart()`) |
| Q / Esc | quit |

Typing in the terminal works like the serial console (`?` lists commands).
Settings persist in `sim/data/`; `make reset` clears them to get the first-boot flow back.

Options: `--scale N` (window zoom), `--headless` (no window), `--script "ms:action[=arg];..."` with
actions `down=x,y`, `move=x,y`, `up`, `shot=file.bmp`, `key=up|down|home`, `quit`
(e.g. `--headless --script "14000:shot=a.bmp;15000:quit"`). Env: `SIM_DATA_DIR`, `SIM_WIFI_FAIL=1`.

## What is real and what is faked

Real: firmware sources, Arduino_GFX (drawing + fonts), U8g2 fonts, Arduino `String`/`Print`, ArduinoJson.

Faked (`shim/`, `sim_runtime.cpp`, `sim_data.cpp`):
- display/touch hardware -> SDL window and mouse
- WiFi -> canned network list, connects 1.5 s after `begin()`
- LittleFS -> host directory, NTP -> host clock
- LibreLinkUp / Dexcom -> synthetic glucose curve (needs any non-empty account, like the real flow)
- web server, OTA, watchdog, mDNS -> no-ops (`src/Server.cpp`, `Libreview.cpp`, `Dexcom.cpp` are not built)

Not covered: network/HTTP code paths, the web UI, and timing/memory limits of the real ESP32-S3.
