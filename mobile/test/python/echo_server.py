from http.server import BaseHTTPRequestHandler
from http.server import HTTPServer
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


if __name__ == "__main__":
    server = HTTPServer(("127.0.0.1", 8080), EchoServerHandler)
    server.serve_forever()
