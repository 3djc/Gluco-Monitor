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
| mouse click | touch (on the settings menu the firmware wants a press held > 300 ms; the simulator holds taps long enough) |
| mouse drag | swipe between pages / scroll (drag speed is amplified: the firmware only detects fast flicks) |
| Left / Right | swipe previous / next page (on a sub-page: back to the settings menu) |
| PgUp / PgDn | scroll lists and the history page |
| Up / Down | glucose +10 / -10 mg/dL |
| Home | reset glucose offset |
| S | save `screenshot-N.bmp` |
| R | restart (like `ESP.restart()`) |
| Q / Esc | quit |

Typing in the terminal works like the serial console (`?` lists commands).
Settings persist in `sim/data/`; `make reset` clears them to get the first-boot flow back.

Options: `--scale N` (window zoom), `--headless` (no window), `--script "ms:action[=arg];..."` with
actions `down=x,y`, `move=x,y`, `up` (raw touch), `mdown=x,y`, `mmove=x,y`, `mup=x,y` (real SDL mouse events, need a window e.g. `SDL_VIDEODRIVER=dummy`), `shot=file.bmp`, `key=up|down|home|left|right|pageup|pagedown`, `quit`
(e.g. `--headless --script "14000:shot=a.bmp;15000:quit"`). Env: `SIM_DATA_DIR`, `SIM_WIFI_FAIL=1`, `SIM_DEBUG=1` (log mouse events).

## What is real and what is faked

Real: firmware sources, Arduino_GFX (drawing + fonts), U8g2 fonts, Arduino `String`/`Print`, ArduinoJson.

Faked (`shim/`, `sim_runtime.cpp`, `sim_data.cpp`):
- display/touch hardware -> SDL window and mouse
- WiFi -> canned network list, connects 1.5 s after `begin()`
- LittleFS -> host directory, NTP -> host clock
- LibreLinkUp / Dexcom -> synthetic glucose curve (needs any non-empty account, like the real flow)
- web server, OTA, watchdog, mDNS -> no-ops (`src/Server.cpp`, `Libreview.cpp`, `Dexcom.cpp` are not built)

Not covered: network/HTTP code paths, the web UI, and timing/memory limits of the real ESP32-S3.
