import random
import sys
from http.server import HTTPServer
from threading import Event
from threading import Thread

import pytest

import envoy_engine
from library.python.envoy_requests.common.engine import Engine
from library.python.envoy_requests import pre_build_engine
from test.python.echo_server import EchoServerHandler


@pytest.fixture(scope="session")
def http_server_url():
    Engine.log_level = envoy_engine.LogLevel.Debug
    pre_build_engine()

    ip = "127.0.0.1"
    # multiple tests may be running at the same time
    # and we do not want their ports to clash
    port = random.randint(2**14, 2**16)

    start_server = Event()
    kill_server = Event()

    def _run_http_server():
        server = HTTPServer((ip, port), EchoServerHandler)
        server.timeout = 0.25
        while True:
            if kill_server.is_set():
                break
            try:
                if not start_server.is_set():
                    start_server.set()
                server.handle_request()
            except Exception as e:
                print(f"Encountered exception: {str(e)}", file=sys.stderr)

    server = Thread(target=_run_http_server)
    server.start()
    start_server.wait()
    yield f"http://{ip}:{port}/"
    kill_server.set()
    server.join()


def pytest_configure(config):
    markers = [
        "asyncio",  # used for async tests
        "standalone",  # used for tests that must run in their own process
    ]
    for marker in markers:
        config.addinivalue_line("markers", marker)
