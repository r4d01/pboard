#!/usr/bin/env python3
"""Optional weather image source for PocketBook Dashboard; standard library + Pillow."""
import argparse
import datetime as dt
import io
import json
import logging
import math
import os
from pathlib import Path
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlencode, urlsplit
from urllib.request import urlopen
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 758, 1024
LOG = logging.getLogger("dashboard")
WEATHER = {
    0: "Clear sky", 1: "Mostly clear", 2: "Partly cloudy", 3: "Overcast",
    45: "Fog", 48: "Freezing fog", 51: "Light drizzle", 53: "Drizzle", 55: "Heavy drizzle",
    56: "Freezing drizzle", 57: "Freezing drizzle", 61: "Light rain", 63: "Rain", 65: "Heavy rain",
    66: "Freezing rain", 67: "Freezing rain", 71: "Light snow", 73: "Snow", 75: "Heavy snow",
    77: "Snow grains", 80: "Rain showers", 81: "Rain showers", 82: "Heavy showers",
    85: "Snow showers", 86: "Heavy snow showers", 95: "Thunderstorm", 96: "Storm with hail", 99: "Storm with hail",
}

def validate_config(config):
    for name, limit in (("latitude", 90), ("longitude", 180)):
        value = config.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not -limit <= value <= limit:
            raise ValueError(f"Set {name} to a number between {-limit} and {limit} in config.json")
    ZoneInfo(config.get("timezone", "UTC"))
    interval = config.get("refresh_seconds", 1200)
    if isinstance(interval, bool) or not isinstance(interval, int) or not 900 <= interval <= 86400:
        raise ValueError("refresh_seconds must be an integer between 900 and 86400")
    port = config.get("port", 8080)
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError("port must be an integer between 1 and 65535")
    if not isinstance(config.get("location", ""), str):
        raise ValueError("location must be a string")
    return config

def fetch_weather(config):
    params = {
        "latitude": config["latitude"], "longitude": config["longitude"],
        "timezone": config.get("timezone", "UTC"), "timeformat": "unixtime", "forecast_days": 4,
        "current": "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m",
        "hourly": "temperature_2m,precipitation_probability",
        "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
    }
    with urlopen("https://api.open-meteo.com/v1/forecast?" + urlencode(params), timeout=20) as response:
        return json.load(response)

def font(size, bold=False):
    override = os.environ.get("WEATHER_FONT_BOLD" if bold else "WEATHER_FONT")
    filename = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    for path in (override, "/usr/share/fonts/truetype/dejavu/" + filename, filename):
        if path:
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                pass
    raise RuntimeError("Install DejaVu Sans fonts or set WEATHER_FONT and WEATHER_FONT_BOLD")

def number(value, suffix="", missing="--"):
    if value is None:
        return missing
    return f"{float(value):.0f}{suffix}"

def render_weather(data, config, fetched_at, demo=False):
    image = Image.new("L", (WIDTH, HEIGHT), 255)
    draw = ImageDraw.Draw(image)
    zone = ZoneInfo(config.get("timezone", "UTC"))
    fetched = dt.datetime.fromtimestamp(fetched_at, zone)

    def label(x, y, value, size=24, bold=False, max_width=None):
        value = str(value)
        f = font(size, bold)
        if max_width:
            while size > 16 and draw.textlength(value, font=f) > max_width:
                size -= 1
                f = font(size, bold)
            if draw.textlength(value, font=f) > max_width:
                while value and draw.textlength(value + "…", font=f) > max_width:
                    value = value[:-1]
                value += "…"
        draw.text((x, y), value, font=f, fill=0, anchor="lt")

    def rule(y):
        draw.line((32, y, WIDTH - 32, y), fill=0, width=2)

    current, hourly, daily = data["current"], data["hourly"], data["daily"]
    label(32, 24, "WEATHER / DEMO DATA" if demo else "WEATHER", 20, True)
    label(32, 59, config.get("location") or "Weather", 38, True, 694)
    label(32, 111, fetched.strftime("%A, %d %B"), 24)
    rule(156)
    label(26, 167, number(current.get("temperature_2m"), "°"), 122, True, 430)
    label(35, 311, WEATHER.get(current.get("weather_code"), "Unknown conditions"), 35, True, 415)
    label(35, 361, "Feels like " + number(current.get("apparent_temperature"), "°"), 26, max_width=415)
    label(470, 196, "WIND", 20, True)
    label(470, 225, number(current.get("wind_speed_10m"), " km/h"), 31, max_width=250)
    label(470, 284, "HUMIDITY", 20, True)
    label(470, 313, number(current.get("relative_humidity_2m"), "%"), 31)
    rule(411)
    label(32, 435, "NEXT HOURS", 21, True)
    future = [i for i, stamp in enumerate(hourly["time"]) if stamp >= fetched_at][:12:3]
    for col, i in enumerate(future):
        x = 32 + 176 * col
        hour = dt.datetime.fromtimestamp(hourly["time"][i], zone)
        label(x, 480, hour.strftime("%H:%M"), 25)
        label(x, 520, number(hourly["temperature_2m"][i], "°"), 41, True)
        label(x, 580, "Rain " + number(hourly["precipitation_probability"][i], "%"), 20, max_width=164)
    rule(632)
    label(32, 654, "OUTLOOK", 21, True)
    days = [i for i, stamp in enumerate(daily["time"]) if dt.datetime.fromtimestamp(stamp, zone).date() >= fetched.date()][:3]
    for row, i in enumerate(days):
        y = 703 + 64 * row
        day = dt.datetime.fromtimestamp(daily["time"][i], zone)
        label(32, y, "Today" if day.date() == fetched.date() else day.strftime("%a"), 25, True, 105)
        label(143, y, WEATHER.get(daily["weather_code"][i], "Unknown"), 23, max_width=270)
        temperatures = number(daily["temperature_2m_max"][i], "°") + " / " + number(daily["temperature_2m_min"][i], "°")
        label(432, y, temperatures, 25, max_width=180)
        label(633, y, number(daily["precipitation_probability_max"][i], "%"), 23, max_width=93)
    rule(903)
    label(32, 922, "Fetched " + fetched.strftime("%d %b %H:%M %Z"), 20, max_width=694)
    model = dt.datetime.fromtimestamp(current["time"], zone).strftime("%H:%M")
    label(32, 956, f"Model {model}  |  Open-Meteo · CC BY 4.0", 17, max_width=694)
    return image

def encode_images(image):
    bmp, png = io.BytesIO(), io.BytesIO()
    # The PocketBook 626's loader rejected 1-bit BMPs in device testing.
    # Pillow writes an uncompressed 8-bit BMP with a 256-entry gray palette.
    image.convert("L").save(bmp, "BMP")
    image.save(png, "PNG")
    return bmp.getvalue(), png.getvalue()

class WeatherCache:
    def __init__(self, config, data_dir="data"):
        self.config = config
        self.path = Path(data_dir) / "weather.json"
        self.lock = threading.Lock()
        self.bmp = self.png = None
        self.fetched_at = None
        self.last_error = None
        self.key = [config["latitude"], config["longitude"], config.get("timezone", "UTC"), config.get("location", "")]
        try:
            saved = json.loads(self.path.read_text())
            if saved["location_key"] == self.key:
                self.install(saved["data"], saved["fetched_at"])
        except FileNotFoundError:
            pass
        except (ValueError, KeyError, TypeError, OSError) as error:
            LOG.warning("Cannot restore weather cache: %s", error)

    def install(self, data, fetched_at):
        bmp, png = encode_images(render_weather(data, self.config, fetched_at))
        with self.lock:
            self.bmp, self.png, self.fetched_at = bmp, png, fetched_at
            self.last_error = None

    def refresh(self, fetcher=fetch_weather):
        try:
            data = fetcher(self.config)
            fetched_at = time.time()
            self.install(data, fetched_at)
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix(".tmp")
            temporary.write_text(json.dumps({"data": data, "fetched_at": fetched_at, "location_key": self.key}))
            temporary.replace(self.path)
            LOG.info("Weather refreshed")
            return True
        except Exception as error:
            # Do not relabel the last good image with a fresh timestamp after a fetch failure.
            with self.lock:
                self.last_error = str(error)
            LOG.warning("Refresh failed; keeping cached image: %s", error)
            return False

    def status(self):
        with self.lock:
            return {"available": self.bmp is not None, "fetched_at": self.fetched_at,
                    "age_seconds": None if self.fetched_at is None else max(0, int(time.time() - self.fetched_at)),
                    "last_error": self.last_error}

def make_handler(cache):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = urlsplit(self.path).path
            status = 200
            if path == "/health":
                body, content_type = json.dumps(cache.status()).encode(), "application/json"
            elif path in ("/weather.bmp", "/weather.png"):
                with cache.lock:
                    body = cache.bmp if path.endswith("bmp") else cache.png
                content_type = "image/bmp" if path.endswith("bmp") else "image/png"
                if body is None:
                    status, body, content_type = 503, b"No weather image yet. Check /health and server logs.\n", "text/plain"
            elif path == "/":
                body = ("<!doctype html><html lang=en><meta charset=utf-8><title>Dashboard image server</title>"
                        "<style>body{font-family:system-ui;max-width:800px;margin:2rem auto;padding:1rem}"
                        "img{max-width:100%;border:1px solid #888}</style><h1>Dashboard image server</h1>"
                        "<p>Reader source: <a href=/weather.bmp>/weather.bmp</a> · <a href=/health>Server status</a></p>"
                        "<img src=/weather.png alt='Latest weather image'><p>Weather data: "
                        "<a href=https://open-meteo.com/>Open-Meteo</a>, "
                        "<a href=https://creativecommons.org/licenses/by/4.0/>CC BY 4.0</a>.</p></html>").encode()
                content_type = "text/html; charset=utf-8"
            else:
                status, body, content_type = 404, b"Not found\n", "text/plain"
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, fmt, *args):
            LOG.info("%s %s", self.address_string(), fmt % args)
    return Handler

def demo_data(now=None):
    now = int(now or time.time())
    hour = now - now % 3600
    midnight = now - now % 86400
    return {
        "current": {"time": now, "temperature_2m": 12, "apparent_temperature": 10,
                    "weather_code": 2, "wind_speed_10m": 14, "relative_humidity_2m": 72},
        "hourly": {"time": [hour + i * 3600 for i in range(48)],
                   "temperature_2m": [12 + (i % 8) for i in range(48)],
                   "precipitation_probability": [10 + (i % 4) * 10 for i in range(48)]},
        "daily": {"time": [midnight + i * 86400 for i in range(4)], "weather_code": [2, 61, 3, 0],
                  "temperature_2m_max": [17, 15, 16, 19], "temperature_2m_min": [8, 9, 7, 9],
                  "precipitation_probability_max": [20, 75, 35, 10]},
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="config.json")
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--demo", metavar="PNG", help="Write a clearly labelled synthetic preview and exit")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if args.demo:
        render_weather(demo_data(), {"timezone": "UTC", "location": "Sample town"}, time.time(), demo=True).save(args.demo)
        return
    config = validate_config(json.loads(Path(args.config).read_text()))
    cache = WeatherCache(config, args.data_dir)
    stop = threading.Event()

    def updater():
        while not stop.is_set():
            cache.refresh()
            stop.wait(config.get("refresh_seconds", 1200))

    worker = threading.Thread(target=updater, daemon=True)
    worker.start()
    server = ThreadingHTTPServer(("0.0.0.0", config.get("port", 8080)), make_handler(cache))
    LOG.info("Dashboard image server listening on port %d", server.server_port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()

if __name__ == "__main__":
    main()
