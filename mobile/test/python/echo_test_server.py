from http.server import BaseHTTPRequestHandler
from http.server import HTTPServer
from threading import Event
from threading import Thread
import json


class EchoServerHandler(BaseHTTPRequestHandler):

    def do_GET(self):
        self._serve_echo()

    def do_HEAD(self):
        self._serve_echo()

    def do_POST(self):
        self._serve_echo()

    def do_PUT(self):
        self._serve_echo()

    def do_DELETE(self):
        self._serve_echo()

    def do_CONNECT(self):
        self._serve_echo()

    def do_OPTIONS(self):
        self._serve_echo()

    def do_TRACE(self):
        self._serve_echo()

    def do_PATCH(self):
        self._serve_echo()

    def _serve_echo(self):
        content_length = self.headers.get("content-length")
        charset = self.headers.get("charset") or "utf8"
        path = self.path or "/"
        if path == "/notfound":
            self.send_error(404, "Not Found")
            return
        if content_length is not None:
            content_length = int(content_length)
            body = self.rfile.read(content_length).decode(charset)
        else:
            body = ""

        request_data = {
            "body": body,
            "headers": {key: self.headers[key] for key in self.headers.keys()},
            "method": self.command,
            "path": self.path,
        }
        self.send_response(200, "OK")
        self.send_header("content-type", "application/json")
        self.send_header("charset", charset)
        self.end_headers()
        self.wfile.write(bytes(json.dumps(request_data), charset))
        self.wfile.write(bytes("\r\n" * 2, charset))

    def log_message(self, *args, **kwargs):
        # we squelch the HTTP server
        # since these logs aren't actually useful
        # compared to envoy logs
        pass


class EchoTestServer:

    def __init__(self, ip, port):
        self._server = HTTPServer((ip, port), EchoServerHandler)
        self._server_thread = Thread(target=self._server.serve_forever, daemon=True)

    def start(self):
        self._server_thread.start()

    def stop(self):
        self._server.shutdown()
        self._server_thread.join()
