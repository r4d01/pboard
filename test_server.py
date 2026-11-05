import io
import json
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import urlopen
from PIL import Image
from server import WeatherCache, demo_data, encode_images, make_handler, render_weather, validate_config

CONFIG = {"latitude": 50.08, "longitude": 14.43, "timezone": "Europe/Prague", "location": "Test town"}

class ServerTests(unittest.TestCase):
    def test_image_formats(self):
        image = render_weather(demo_data(), CONFIG, time.time())
        bmp, png = encode_images(image)
        decoded = Image.open(io.BytesIO(bmp))
        self.assertEqual(decoded.size, (758, 1024))
        self.assertEqual(decoded.mode, "L")
        self.assertLess(len(bmp), 1_500_000)
        self.assertEqual(int.from_bytes(bmp[14:18], "little"), 40)
        self.assertEqual(int.from_bytes(bmp[28:30], "little"), 8)
        self.assertEqual(int.from_bytes(bmp[30:34], "little"), 0)
        self.assertEqual(Image.open(io.BytesIO(png)).size, (758, 1024))

    def test_failure_preserves_image_and_cache_matches_location(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = WeatherCache(CONFIG, directory)
            self.assertTrue(cache.refresh(lambda _: demo_data()))
            original = (cache.bmp, cache.fetched_at)
            def fail(_):
                raise RuntimeError("simulated upstream failure")
            self.assertFalse(cache.refresh(fail))
            self.assertEqual((cache.bmp, cache.fetched_at), original)
            self.assertIn("upstream", cache.status()["last_error"])
            restored = WeatherCache(CONFIG, directory)
            self.assertEqual((restored.bmp, restored.fetched_at), original)
            changed = WeatherCache(dict(CONFIG, latitude=49), directory)
            self.assertIsNone(changed.bmp)

    def test_http_routes_and_initial_503(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = WeatherCache(CONFIG, directory)
            server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(cache))
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            try:
                for route, code in (("/weather.bmp", 503), ("/config.json", 404)):
                    with self.assertRaises(HTTPError) as context:
                        urlopen(base + route)
                    self.assertEqual(context.exception.code, code)
                cache.refresh(lambda _: demo_data())
                with urlopen(base + "/weather.bmp") as response:
                    self.assertEqual(response.headers["Content-Type"], "image/bmp")
                    self.assertEqual(response.read(), cache.bmp)
                with urlopen(base + "/health") as response:
                    self.assertTrue(json.load(response)["available"])
            finally:
                server.shutdown()
                server.server_close()

    def test_config_rejects_missing_location_and_unsafe_interval(self):
        self.assertEqual(validate_config(CONFIG), CONFIG)
        for config in ({}, dict(CONFIG, latitude=float("nan")), dict(CONFIG, refresh_seconds=0)):
            with self.assertRaises(ValueError):
                validate_config(config)

if __name__ == "__main__":
    unittest.main()
