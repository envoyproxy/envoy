import sys
from http.server import HTTPServer
from threading import Event
from threading import Thread

import pytest

from library.python.envoy_requests.common.engine import Engine
from test.python.echo_server import EchoServerHandler


@pytest.fixture(scope="session")
def http_server_url():
    Engine.build()

    ip = "127.0.0.1"
    port = 9876

    kill_server = Event()

    def _run_http_server():
        server = HTTPServer((ip, port), EchoServerHandler)
        server.timeout = 0.25
        while True:
            if kill_server.is_set():
                break
            try:
                server.handle_request()
            except Exception as e:
                print(f"Encountered exception: {str(e)}", file=sys.stderr)

    server = Thread(target=_run_http_server)
    server.start()
    yield f"http://{ip}:{port}/"
    kill_server.set()
    server.join()
