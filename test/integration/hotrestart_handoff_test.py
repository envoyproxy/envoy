"""Tests the behavior of connection handoff between instances during hot restart.

Specifically, tests that:
1. TCP connections opened before hot restart begins continue to function during drain.
2. TCP connections opened after hot restart begins while the old instance is still running
   go to the new instance.
TODO(ravenblack): perform the same tests for QUIC connections once they will work as expected.
"""

import asyncio
import logging
import os
import pathlib
import random
import sys
from typing import AsyncIterator
import unittest
from datetime import datetime, timedelta
from aiohttp import client_exceptions, web, ClientSession


def random_loopback_host():
    """Returns a randomized loopback IP.
    This can be used to reduce the chance of port conflicts when tests are
    running in parallel."""
    return f"127.{random.randrange(0,256)}.{random.randrange(0,256)}.{random.randrange(1, 255)}"


# This is a timeout that must be long enough that the hot restarted
# instance can reliably be fully started up within this many seconds, or the
# test will be flaky. 3 seconds is enough on a not-busy host with a non-tsan
# non-coverage build; 10 seconds should be enough to be not flaky in most
# configurations.
#
# Unfortunately, because the test is verifying the behavior of a connection
# during drain, the first connection must last for the full tolerance duration,
# so increasing this value increases the duration of the test. For this
# reason we want to keep it as low as possible without causing flaky failure.
#
# Ideally this would be adjusted (3x) for tsan and coverage runs, but making that
# possible for python is outside the scope of this test, so we're stuck using the
# 3x value for all tests.
STARTUP_TOLERANCE_SECONDS = 10

# We send multiple requests in parallel and require them all to function correctly
# - this makes it so if something is flaky we're more likely to encounter it, and
# also tests that there's not an "only one" success situation.
PARALLEL_REQUESTS = 5

UPSTREAM_SLOW_PORT = 54321
UPSTREAM_FAST_PORT = 54322
UPSTREAM_HOST = random_loopback_host()
ENVOY_HOST = UPSTREAM_HOST
ENVOY_PORT = 54323
ENVOY_ADMIN_PORT = 54324
# Append process ID to the socket path to minimize chances of
# conflict. We can't use TEST_TMPDIR for this because it makes
# the socket path too long.
SOCKET_PATH = f"@envoy_domain_socket_{os.getpid()}"
SOCKET_MODE = 0
ENVOY_BINARY = "./test/integration/hotrestart_main"

# This log config makes logs interleave with other test output, which
# is useful since with all the async operations it can be hard to figure
# out what's happening.
log = logging.getLogger()
log.level = logging.INFO
_stream_handler = logging.StreamHandler(sys.stdout)
log.addHandler(_stream_handler)


class Upstream:
    # This class runs a server which takes an http request to
    # path=/ and responds with "start\n" [three second pause] "end\n".
    # This allows us to test that during hot restart an already-opened
    # connection will persist.
    # If initialized with True it will instead respond with
    # "fast instance" immediately.
    def __init__(self, fast_version=False):
        self.port = UPSTREAM_FAST_PORT if fast_version else UPSTREAM_SLOW_PORT
        self.app = web.Application()
        self.app.add_routes([
            web.get('/', self.fast_response) if fast_version else web.get('/', self.slow_response),
        ])

    async def start(self):
        self.runner = web.AppRunner(self.app, handle_signals=False)
        await self.runner.setup()
        site = web.TCPSite(self.runner, host=UPSTREAM_HOST, port=self.port)
        await site.start()

    async def stop(self):
        await self.runner.shutdown()
        await self.runner.cleanup()
        log.debug("runner cleaned up")

    async def fast_response(self, request):
        return web.Response(
            status=200, reason='OK', headers={'content-type': 'text/plain'}, body='fast instance')

    async def slow_response(self, request):
        log.debug("slow request received")
        response = web.StreamResponse(
            status=200, reason='OK', headers={'content-type': 'text/plain'})
        await response.prepare(request)
        await response.write(b"start\n")
        await asyncio.sleep(STARTUP_TOLERANCE_SECONDS + 0.5)
        await response.write(b"end\n")
        await response.write_eof()
        return response


async def _http_request(url) -> AsyncIterator[str]:
    # Separate session per request is against aiohttp idioms, but is
    # intentional here because the point of the test is verifying
    # where connections go - reusing a connection would do the wrong thing.
    async with ClientSession() as session:
        async with session.get(url) as response:
            async for line in response.content:
                yield line


async def _full_http_request(url: str) -> str:
    # Separate session per request is against aiohttp idioms, but is
    # intentional here because the point of the test is verifying
    # where connections go - reusing a connection would do the wrong thing.
    async with ClientSession() as session:
        async with session.get(url) as response:
            return await response.text()


def _make_envoy_config_yaml(upstream_port, file_path):
    file_path.write_text(
        f"""
admin:
  address:
    socket_address:
      address: {ENVOY_HOST}
      port_value: {ENVOY_ADMIN_PORT}

static_resources:
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: {ENVOY_HOST}
        port_value: {ENVOY_PORT}
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          codec_type: AUTO
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains: ["*"]
              routes:
              - match:
                  prefix: "/"
                route:
                  cluster: some_service
          http_filters:
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: some_service
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: some_service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {UPSTREAM_HOST}
                port_value: {upstream_port}
""")


async def _wait_for_envoy_epoch(i: int):
    """Load the admin/server_info page until restart_epoch is i, or timeout"""
    expected_substring = f'"restart_epoch": {i}'
    deadline = datetime.now() + timedelta(seconds=STARTUP_TOLERANCE_SECONDS)
    response = "admin port not responding within timeout"
    while datetime.now() < deadline:
        try:
            response = await _full_http_request(
                f"http://{ENVOY_HOST}:{ENVOY_ADMIN_PORT}/server_info")
            if expected_substring in response:
                return
        except client_exceptions.ClientConnectorError:
            pass
        await asyncio.sleep(0.2)
    # Envoy instance with expected restart_epoch should have started up
    assert expected_substring in response, f"server_info={response}"


class IntegrationTest(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self) -> None:
        print(os.environ)
        tmpdir = os.environ["TEST_TMPDIR"]
        self.slow_config_path = pathlib.Path(tmpdir, "slow_config.yaml")
        self.fast_config_path = pathlib.Path(tmpdir, "fast_config.yaml")
        self.base_id_path = pathlib.Path(tmpdir, "base_id.txt")
        _make_envoy_config_yaml(upstream_port=UPSTREAM_SLOW_PORT, file_path=self.slow_config_path)
        _make_envoy_config_yaml(upstream_port=UPSTREAM_FAST_PORT, file_path=self.fast_config_path)
        self.base_envoy_args = [
            ENVOY_BINARY,
            "--socket-path",
            SOCKET_PATH,
            "--socket-mode",
            str(SOCKET_MODE),
        ]
        log.info("starting upstreams")
        await super().asyncSetUp()
        self.slow_upstream = Upstream()
        await self.slow_upstream.start()
        self.fast_upstream = Upstream(True)
        await self.fast_upstream.start()

    async def asyncTearDown(self) -> None:
        await self.slow_upstream.stop()
        await self.fast_upstream.stop()
        return await super().asyncTearDown()

    async def test_connection_handoffs(self) -> None:
        log.info("starting envoy")
        envoy_process_1 = await asyncio.create_subprocess_exec(
            *self.base_envoy_args,
            "--restart-epoch",
            "0",
            "--use-dynamic-base-id",
            "--base-id-path",
            self.base_id_path,
            "-c",
            self.slow_config_path,
        )
        log.info("waiting for envoy ready")
        await _wait_for_envoy_epoch(0)
        log.info("making requests")
        slow_responses = [
            _http_request(f"http://{ENVOY_HOST}:{ENVOY_PORT}/") for i in range(PARALLEL_REQUESTS)
            # TODO(ravenblack): add http3 slow requests
        ]
        log.info("waiting for responses to begin")
        for response in slow_responses:
            self.assertEqual(await anext(response, None), b"start\n")
        base_id = int(self.base_id_path.read_text())
        log.info(f"starting envoy hot restart for base id {base_id}")
        envoy_process_2 = await asyncio.create_subprocess_exec(
            *self.base_envoy_args,
            "--restart-epoch",
            "1",
            "--parent-shutdown-time-s",
            str(STARTUP_TOLERANCE_SECONDS + 1),
            "--base-id",
            str(base_id),
            "-c",
            self.fast_config_path,
        )
        log.info("waiting for new envoy instance to begin")
        await _wait_for_envoy_epoch(1)
        log.info("sending request to fast upstream")
        fast_responses = [
            _full_http_request(f"http://{ENVOY_HOST}:{ENVOY_PORT}/")
            for i in range(PARALLEL_REQUESTS)
            # TODO(ravenblack): add http3 requests
        ]
        for response in fast_responses:
            self.assertEqual(
                await response, "fast instance",
                "new requests after hot restart begins should go to new cluster")

        # Now wait for the slow request to complete, and make sure it still gets the
        # response from the old instance.
        log.info("waiting for completion of original slow request")
        t1 = datetime.now()
        for response in slow_responses:
            self.assertEqual(await anext(response, None), b"end\n")
        t2 = datetime.now()
        self.assertGreater(
            (t2 - t1).total_seconds(), 0.5,
            "slow request should be incomplete when the test waits for it, otherwise the test is not necessarily validating during-drain behavior"
        )
        for response in slow_responses:
            self.assertIsNone(await anext(response, None))
        log.info("waiting for parent instance to terminate")
        await envoy_process_1.wait()
        log.info("sending second request to fast upstream")
        fast_responses = [
            _full_http_request(f"http://{ENVOY_HOST}:{ENVOY_PORT}/")
            for i in range(PARALLEL_REQUESTS)
            # TODO(ravenblack): add http3 requests
        ]
        for response in fast_responses:
            self.assertEqual(
                await response, "fast instance",
                "new requests after old instance terminates should go to new cluster")
        log.info("shutting child instance down")
        envoy_process_2.terminate()
        await envoy_process_2.wait()


if __name__ == '__main__':
    unittest.main()
