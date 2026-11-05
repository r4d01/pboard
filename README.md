# PocketBook Dashboard

A native, full-screen image dashboard for the **PocketBook 626 Touch Lux 3**. The reader downloads an image from your server, displays it with a small status bar, disconnects Wi-Fi, and requests timed sleep until the next refresh. Weather is an optional image source; the reader application is named **Dashboard**.

**Prototype:** A PocketBook 626 running firmware U626.5.20.1157 displayed an 8-bit BMP and completed one timed sleep; it also repeatedly refused sleep after a later refresh with the original build. This revision changes sleep handling and diagnostics, but has not yet been tested on the reader. Two weeks is a target, not a measured result. No rooting or firmware replacement is included.

## What is included

- Native InkView application, source and a compiled ARM executable.
- Startup settings for an HTTP(S) image URL, refresh interval, and quiet hours with minute precision.
- Full-screen image area and an 88-pixel bottom status bar with connection/download/refresh messages, errors, last successful download, next scheduled update, battery percentage and charging state.
- Default refresh every **20 minutes**, with **22:00–07:00** quiet hours. Failed downloads retain the last successful image and use the normal interval for the next attempt.
- A Docker build for the reader application and a separate Docker service that renders Open-Meteo weather on your always-on machine.
- Host tests for settings, scheduling, HTTP failures, image validation, cache retention and sleep refusal.

## Install the reader application

1. Download this repository using **Code → Download ZIP**, or clone it:

   ```sh
   git clone https://github.com/r4d01-ai/pboard.git
   cd pboard
   ```

2. Connect the PocketBook by USB. Copy the **contents** of `reader/applications/` into the reader's existing `applications/` folder. The resulting paths on the reader are:

   ```text
   applications/dashboard.app
   applications/dashboard/dashboard.cfg
   ```

3. Safely eject and disconnect USB. Configure a working Wi-Fi connection in the reader's normal settings, turn the frontlight off, and check its clock.
4. Open `dashboard.app` from **Applications**. Enter the source URL, choose the refresh interval and quiet hours, then tap **Save and start dashboard**. Settings appear on every launch; the application does not install an automatic boot task.

Tap the bottom status bar, or use the Menu key, to reopen settings. Page/arrow keys select settings and OK edits the selected field. Home/Back exits when the application is not downloading. Network operations are synchronous, so input may wait while connecting/downloading.

The configuration file is optional: defaults are created on first save if only `dashboard.app` was copied. When upgrading, replace the executable while it is closed and keep your existing configuration. If an earlier `weather.app` prototype is installed, exit it before starting Dashboard.

**Downloading in the PocketBook browser is not a verified installer.** A browser download only works as an installation if the firmware lets you save or move the executable into `applications/`. Otherwise use USB for the first installation. This project does not modify the browser, register download handlers, or offer an automatic updater. A direct binary is available at [reader/applications/dashboard.app](reader/applications/dashboard.app).

## Start the optional weather source

On your always-on machine with Docker Compose:

```sh
cp config.example.json config.json
# Edit config.json: supply your latitude, longitude, location label and timezone.
docker compose up -d --build
```

The example intentionally leaves latitude/longitude empty. Set both before starting. Give the server a stable LAN address or DHCP reservation. Open `http://SERVER_IP:8080/` on another device to check the image, then set the reader URL to:

```text
http://SERVER_IP:8080/weather.bmp
```

Use the server's LAN IP, not `localhost`. The example port is 8080. If you change the internal port in `config.json`, also change the Compose port mapping. Keep this unauthenticated image service on your trusted LAN; it is not designed as a public hosting service.

The server fetches and renders weather in the background every 20 minutes, so a reader request only transfers a cached image. `/weather.png` provides a browser preview and `/health` reports availability, the age of the data and the last error. Before the first successful fetch, image requests return HTTP 503. Upstream failures retain the cached image and its original timestamp. Cache files survive container restarts in `data/`.

“Last OK” in the reader bar means **the image download succeeded**. It does not mean the weather data is fresh: check the **Fetched** timestamp inside the weather image or the server's `/health` endpoint. The weather source uses Celsius and km/h. It needs internet access; the reader only needs access to the LAN image server.

For local development without Docker:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python server.py --config config.json
```

Install DejaVu Sans fonts (`fonts-dejavu-core` on Debian/Ubuntu), or set `WEATHER_FONT` and `WEATHER_FONT_BOLD` to suitable TrueType files.

## Other dashboards and photographs

The reader accepts a direct **uncompressed Windows BMP** URL: 40-byte BITMAPINFOHEADER, positive dimensions, at most 2048 × 2048 pixels and 2 MiB. The app's header check accepts 1-, 8- or 24-bit pixels, but on the tested PocketBook 626 the firmware loader rejected a valid 1-bit BMP. **Use an 8-bit grayscale BMP with a 256-entry palette**, as generated by the included server. It rejects incomplete data and keeps the previous image on failure. HTML, JavaScript dashboards, PNG/JPEG, redirects, login pages and compressed BMP variants are not supported directly.

Render any dashboard or photo on your server, convert it to BMP, and replace the served file atomically. A **758 × 936** image fills the available image area exactly. Other aspect ratios fit without cropping, with white margins. The optional weather source is 758 × 1024 and is scaled to 692 × 936 above the status bar. A grayscale 8-bit BMP supports both photographs and black-and-white images.

Example conversion with Pillow:

```python
from PIL import Image, ImageOps
image = ImageOps.exif_transpose(Image.open("photo.jpg")).convert("L")
image = ImageOps.pad(image, (758, 936), color=255)
image.save("dashboard.tmp.bmp", "BMP")
from pathlib import Path
Path("dashboard.tmp.bmp").replace("dashboard.bmp")
```

Use a direct LAN HTTP endpoint for maximum compatibility with old firmware. HTTPS uses the reader's libcurl and certificate store; certificate verification remains enabled. Modern TLS endpoints may not work with the reader's old libraries. Do not put credentials in a public URL; the configured URL is stored as plain text on the reader.

## Schedule and power behavior

Quiet hours use the **reader's local clock**, displayed in settings. The weather server's timezone does not set the reader clock. A matching quiet start and end disables quiet hours; intervals can cross midnight. No scheduled downloads occur during quiet hours, and an update due inside that interval is moved to the end of quiet hours. Starting the app at night shows the cache, if one is available, without connecting.

The advanced `timezone=` setting in `applications/dashboard/dashboard.cfg` is optional. Leave it empty to use the firmware's timezone. If the displayed local clock is wrong despite correct device settings, a POSIX timezone can be supplied, for example `CET-1CEST,M3.5.0,M10.5.0/3` for Central European daylight-saving rules. Do not assume an IANA zone database is installed on this old reader.

After each attempt, Dashboard disconnects Wi-Fi, waits five seconds for the display and network to settle, and calls the SDK's `GoSleep(milliseconds, 0)` with the next deadline. It explicitly disables automatic sleep while downloading and re-enables sleep afterward; it does not call the firmware power-off postponement function during refreshes or just before sleeping. These changes are intended to address the reported failure but their effectiveness on the device is unverified. While running, it disables automatic **power-off**, because a powered-off device cannot resume this application through its timer. It re-enables automatic power-off in settings, on exit or if sleep is repeatedly refused. It does not deliberately power the reader off overnight. The image should remain on the e-ink display during sleep.

Battery information is a snapshot taken while already awake; there is no extra battery-polling timer. The frontlight is controlled by the reader's own settings. Turn it off and unplug USB for power testing. The HTTP transfer has a 5-second connection timeout and a 15-second total timeout. The preceding firmware Wi-Fi association call has its own behavior and is not bounded by those curl timeouts.

If twelve consecutive sleep requests return in under two seconds, Dashboard pauses, displays a sleep error and allows normal automatic power-off. Short attempts are retried ten seconds apart, with a longer sleep resetting the count. The log records the SDK return value, sleep mode, USB state, network flags, and Wi-Fi shutdown results. A short return does not prove why sleep failed. This fallback limits battery drain; it is not proof of low-power suspend. Other firmware wake-ups can still occur during quiet hours.

At a 20-minute interval and a 15-hour active window, expect about 45 refreshes/day (630 in 14 days); at 15 minutes, about 60/day (840 in 14 days). Wi-Fi association time, actual suspend support, frontlight use, battery aging and signal strength determine whether the target is achievable.

## Compile using Docker

From the repository root:

```sh
docker buildx build --platform linux/amd64 \
  --file Dockerfile.reader \
  --output type=local,dest=./build .
```

Copy `build/applications/` to the reader as above. `linux/amd64` is the **SDK compiler's host architecture**; the output is ARM for the reader. Apple Silicon needs Docker's amd64 emulation. The first build downloads a large SDK checkout and needs several GB of disk space. Later builds reuse Docker's toolchain layer.

The build uses PocketBook's SDK-A13 at pinned commit `23eb32c3a011a1df4ce3d8f22150dcdd34cbc75a`, runs schedule tests, compiles `dashboard.app` and exports the executable and default configuration. It needs network access to Debian package repositories and GitHub. The weather-server Dockerfile is independent of this cross-compiler.

Without Docker, use a Linux amd64 SDK checkout:

```sh
export PBSDK=/absolute/path/to/SDK_6.3.0/SDK-A13
sh client/build.sh
```

`client/dashboard.app` is dynamically linked to the firmware's `libinkview.so`, `libcurl.so.4` and libc. Compilation does not establish compatibility with every official firmware release. The bundled binary was built with the same pinned SDK; its checksum is in `reader/SHA256SUMS`.

## Tests and first device check

Host requirements: GCC, libcurl development headers, Python 3.10+ with Pillow, and DejaVu Sans. On Debian/Ubuntu install `gcc libcurl4-openssl-dev fonts-dejavu-core` in addition to Python.

```sh
mkdir -p build
gcc -std=gnu99 -Wall -Wextra -Werror client/test_schedule.c -o build/test-schedule
build/test-schedule
python client/tests/run_tests.py
python -m unittest -v test_server.py
```

The client tests execute real HTTP downloads from a loopback server but mock the reader's display, battery, Wi-Fi and suspend APIs. They test configuration persistence, touch/keyboard navigation, minute boundaries, DST transitions, invalid/oversized images, HTTP/Wi-Fi errors, preservation of the previous image, quiet-hour skipping and sleep-refusal handling. They do **not** emulate firmware power management. The server tests cover image format, cache restoration, upstream failure behavior, config validation and HTTP routes.

For the first physical test, set a one-minute interval briefly, disconnect USB and verify at least three refreshes. Then set quiet hours around the current time and verify the next wake-up is at their end. Restore a 15–20-minute interval, leave the frontlight off, and record battery levels over several days. If it stops refreshing or shows a sleep error, collect `applications/dashboard/dashboard.log` along with the exact firmware version. Logs rotate at approximately 128 KiB and include sleep duration, HTTP status and battery readings. Configuration and cache contain your source URL; review them before sharing.

**Validation:** Host tests exercise the updated retry logic and 8-bit image output using mocked device APIs; they do not emulate PocketBook sleep behavior. The updated binary must be checked on the physical reader. Docker was unavailable in the development workspace, so the Docker recipe has not been executed there. The GitHub workflow provides host tests and a Docker build when run on GitHub Actions.

## Layout and references

| Path | Purpose |
| --- | --- |
| `client/` | Native application, configuration and host tests |
| `reader/` | Ready-to-copy ARM executable and default settings |
| `server.py` | Optional weather image source |
| `Dockerfile.reader` | SDK cross-compile and artifact export |
| `Dockerfile`, `compose.yaml` | Weather source service |
| `docs/weather-demo.png` | Synthetic sample, not live data or a reader screenshot |

Primary references: [PocketBook SDK](https://github.com/pocketbook/SDK_6.3.0), [pinned InkView header](https://github.com/pocketbook/SDK_6.3.0/blob/23eb32c3a011a1df4ce3d8f22150dcdd34cbc75a/SDK-A13/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include/inkview.h), and [Open-Meteo API documentation](https://open-meteo.com/en/docs). Weather data attribution: [Open-Meteo](https://open-meteo.com/), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Check the provider's current usage terms for your deployment.

Project code is MIT licensed. The external SDK, firmware libraries, fonts and weather data retain their respective licenses; they are not included under this project's license.
