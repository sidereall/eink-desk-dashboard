# E-ink Desk Dashboard

![E-ink desk dashboard](docs/hero.png)

A standalone e-ink desk display built around an ESP32-C6 microcontroller, running firmware written in C++ with PlatformIO. It connects over Wi-Fi, syncs its time via NTP, and shows a clock, a task list, weather, and stock market data on a 4.2" e-ink panel, sourced from public APIs (Open-Meteo and Twelve Data). The device is configured entirely from a web page it serves over the local network.

---

## Screens

Four screens cycle on a timer, or one can be pinned, both configurable via the web page. Each shows the battery level and a Wi-Fi icon in the top right corner.

### Clock

Time from NTP, with the timezone chosen from the web page and daylight saving worked out automatically from it. The date line under the clock can be toggled on or off.

<img src="docs/clock.png" width="440" alt="Clock screen, light and dark">

### Tasks

Five slots, edited from the web page. Shows the current date and how many entered tasks are complete. Completed tasks are struck through; deleting one closes the gap.

<img src="docs/tasks.png" width="440" alt="Tasks screen, light and dark">

### Weather

Current conditions with a weather icon and today's high and low, plus a four-day forecast, from Open-Meteo. Location is searched by city name from the web page, along with a choice of Celsius or Fahrenheit. No API key needed.

<img src="docs/weather.png" width="440" alt="Weather screen, light and dark">

### Markets

A quote with the change since the previous close, and a sparkline of the session. The header shows whether the NYSE is open or closed. Any US stock or ETF can be chosen from the web page, searched by name or ticker, with SPY as the default. Needs a free Twelve Data key, also entered from the web page.


<img src="docs/markets.png" width="440" alt="Markets screen, light and dark">

---

## Hardware

Roughly 30€ in parts including the enclosure.

| Part | Notes |
| --- | --- |
| ESP32-C6-DevKit | Any other C6 board should work without changes |
| WeAct 4.2" e-ink screen | 400x300, black and white, SSD1683 controller |
| SM5308 power module | USB-C input, charge and boost |
| 18650 li-ion cell | Used 2200mAh, size depends on desired runtime |
| 2x 82kΩ resistors | Voltage divider for the battery reading |
| 3D printed enclosure | Two .stl files modeled by me, see `enclosure/` |

<br>

<img src="docs/wiring.png" width="440" alt="Wiring diagram">
<br>
<img src="docs/wiring_photo.png" width="440" alt="Assembled electronics">

---

## Building

PlatformIO, with the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform fork. The official PlatformIO only supports the ESP32C6 for ESP-IDF, but not yet for the Arduino framework this project uses.

```bash
git clone https://github.com/sidereall/eink-desk-dashboard
cd eink-desk-dashboard
cp src/secrets.h.example src/secrets.h    # then fill in your Wi-Fi details
```

Two uploads, since the firmware and the web page live in different places:

```bash
pio run -t upload      # firmware
pio run -t uploadfs    # the web page, onto the filesystem partition
```

Skipping the second step leaves the API working, but the config page returns a 404 error.

---

## Setting it up

After connecting to Wi-Fi, the panel shows its IP address. Opening that address in a browser on the same network loads the config page, which also shows the IP from then on.

<img src="docs/webui.png" width="440" alt="The config page">

A status strip along the top shows the battery level, signal strength, device time and which screen is currently up. Below it, each section can be expanded to change its settings, and shows what it's currently set to when collapsed:

- **Clock** - Time zone, and whether to show the date line.
- **Tasks** - The five task slots.
- **Weather** - Location, searched by city name, and Celsius or Fahrenheit.
- **Markets** - The Twelve Data API key, and a search for a US-listed stock or ETF.
- **Display** - Cycle through screens or pin one, the rotation interval, and dark mode.

Each section saves on its own, and the panel redraws immediately. The page reads the device's current settings on load, so opening it from a different browser shows what's already configured. Except the Twelve Data key, which only reports whether one is set.

Settings survive a power cut and a reflash. Holding BOOT for three seconds clears them.

---

## Project structure

```
src/
  main.cpp           |setup, the main loop, and everything that draws
  panel.cpp          |the only file that talks to the display driver
  screens.cpp        |draws each screen from a state struct
  clock_time.cpp     |NTP and the timezone rules
  tasks.cpp          |the task list, in RAM and saved to flash
  weather.cpp        |Open-Meteo fetch, on its own task
  markets.cpp        |Twelve Data fetch, on its own task
  wifi_net.cpp       |the connection and its reconnect handling
  web_ui.cpp         |the config page and its API
  app_settings.cpp   |everything that persists
  config.h           |every tunable number in the project
data/
  index.html         |the config page itself
```

---

## Security

**Protected.** Every write endpoint validates input at the boundary. Secrets never come back out: the markets key reads back only as configured or not, the Wi-Fi password exists only in the compiled firmware, and the key is logged by length alone. The radio won't join anything weaker than WPA2, so an open network with a matching name can't lure the device onto it.

**Not protected.** Any device on the same network can call the write endpoints, the same setup as most consumer devices on the LAN.

**Deliberately not done.** HTTPS would need a self-signed certificate and a browser warning on every visit, worse for the user than the problem it solves. Flash encryption is disproportionate for this scope.

---

## Future plans

- ~~Dark mode for both the panel and the web page.~~
- ~~Choice of any US stock or ETF for the markets screen, not just SPY.~~
- ~~Temperature unit switching, Celsius or Fahrenheit.~~
- ~~Battery charge level monitoring.~~
- ~~Web page UI/UX improvements.~~
- Fix bugs surfacing from longer-term use.
- Wi-Fi setup through a temporary access point, instead of compiling credentials in.
- Custom PCB, replacing the current point-to-point wiring.

---
