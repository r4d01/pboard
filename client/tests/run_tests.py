"""Exercise the actual client with a local HTTP server and mocked InkView I/O."""
import io
import pathlib
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
buffer = io.BytesIO()
Image.new("L", (758, 1024), 255).save(buffer, "BMP")
BMP = buffer.getvalue()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        payload = {"/good.bmp": BMP, "/bad.bmp": b"not an image" * 10,
                   "/truncated.bmp": BMP[:60], "/oversize.bmp": b"x" * (2 * 1024 * 1024 + 1)}.get(self.path)
        self.send_response(200 if payload is not None else 404)
        self.end_headers()
        try:
            self.wfile.write(payload or b"missing")
        except (BrokenPipeError, ConnectionResetError):
            pass

def main():
    with tempfile.TemporaryDirectory() as directory:
        binary = str(pathlib.Path(directory) / "test-client")
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "client/tests/test_client.c"), "-lcurl", "-o", binary], check=True)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            subprocess.run([binary, f"http://127.0.0.1:{server.server_port}"], cwd=directory, check=True)
        finally:
            server.shutdown()
            server.server_close()

if __name__ == "__main__":
    main()
