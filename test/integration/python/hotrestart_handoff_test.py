"""Tests the behavior of connection handoff between instances during hot restart.

Specifically, tests that:
1. TCP connections opened before hot restart begins continue to function during drain.
2. TCP connections opened after hot restart begins while the old instance is still running
   go to the new instance.
3. UDP sessions established before hot restart keep being served by the old instance during
   drain, while new UDP sessions are forwarded to the new instance.
TODO(ravenblack): perform the same tests for QUIC connections once they will work as expected.
"""

import abc
import argparse
import asyncio
import contextlib
from functools import cached_property
import logging
import os
import pathlib
import random
import sys
import tempfile
from typing import Awaitable, Self
import unittest
from datetime import datetime, timedelta, timezone
from aiohttp import client_exceptions, web, ClientSession


def random_loopback_host():
    """Returns a randomized loopback IP.
    This can be used to reduce the chance of port conflicts when tests are
    running in parallel."""
    return f"127.{random.randrange(0,256)}.{random.randrange(0,256)}.{random.randrange(1, 255)}"


# This is a timeout that must be long enough that the hot restarted
# instance can reliably be fully started up within this many seconds, or the
# test will be flaky. 3 seconds is enough on a not-busy host with a non-tsan
# non-coverage build; 6 seconds should be enough to be not flaky in most
# configurations.
#
# The slow upstream response is now gated by the test instead of sleeping for
# this duration, so this constant primarily controls startup polling and UDP
# request retry deadlines.
STARTUP_TOLERANCE_SECONDS = 6

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
ENVOY_UDP_PORT = 54325
ENVOY_URL = f"http://{ENVOY_HOST}:{ENVOY_PORT}/"
ENVOY_HTTPS_URL = f"https://{ENVOY_HOST}:{ENVOY_PORT}/"
UPSTREAM_UDP_SLOW_PORT = 54326
UPSTREAM_UDP_FAST_PORT = 54327
# Append process ID to the socket path to minimize chances of
# conflict. We can't use TEST_TMPDIR for this because it makes
# the socket path too long.
SOCKET_PATH = f"@envoy_domain_socket_{os.getpid()}"
SOCKET_MODE = 0

# This log config makes logs interleave with other test output, which
# is useful since with all the async operations it can be hard to figure
# out what's happening.
log = logging.getLogger()
log.level = logging.INFO
_stream_handler = logging.StreamHandler(sys.stdout)
log.addHandler(_stream_handler)


class Upstream:
    # This class runs a server which takes an http request to
    # path=/ and responds with "start\n" [test-controlled delay] "end\n".
    # This allows us to test that during hot restart an already-opened
    # connection will persist.
    # If initialized with True it will instead respond with
    # "fast instance" immediately.
    def __init__(self, fast_version=False):
        self.port = UPSTREAM_FAST_PORT if fast_version else UPSTREAM_SLOW_PORT
        self.release = asyncio.Event()
        self.app = web.Application()
        self.app.add_routes([
            web.get("/", self.fast_response) if fast_version else web.get("/", self.slow_response),
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
            status=200,
            reason="OK",
            headers={"content-type": "text/plain"},
            body="fast instance",
        )

    async def slow_response(self, request):
        log.debug("slow request received")
        response = web.StreamResponse(
            status=200, reason="OK", headers={"content-type": "text/plain"})
        await response.prepare(request)
        await response.write(b"start\n")
        try:
            await asyncio.wait_for(self.release.wait(), timeout=60)
        except asyncio.TimeoutError:
            log.warning("timed out waiting to release slow upstream response")
        await response.write(b"end\n")
        await response.write_eof()
        return response


class LineGenerator:

    @cached_property
    def _queue(self) -> asyncio.Queue[str]:
        return asyncio.Queue()

    @cached_property
    def _task(self):
        return asyncio.create_task(self.generator())

    @abc.abstractmethod
    async def generator(self) -> None:
        raise NotImplementedError

    def __init__(self):
        self._task

    async def join(self) -> int:
        await self._task
        return self._queue.qsize()

    async def line(self) -> str:
        line = await self._queue.get()
        self._queue.task_done()
        return line


class Http3RequestLineGenerator(LineGenerator):

    def __init__(self, url):
        self._url = url
        super().__init__()

    async def generator(self) -> None:
        proc = await asyncio.create_subprocess_exec(
            IntegrationTest.h3_request,
            f"--ca-certs={IntegrationTest.ca_certs}",
            self._url,
            stdout=asyncio.subprocess.PIPE,
        )
        async for line in proc.stdout:
            await self._queue.put(line)
        await proc.wait()


class HttpRequestLineGenerator(LineGenerator):

    def __init__(self, url):
        self._url = url
        super().__init__()

    async def generator(self) -> None:
        # Separate session per request is against aiohttp idioms, but is
        # intentional here because the point of the test is verifying
        # where connections go - reusing a connection would do the wrong thing.
        async with ClientSession() as session:
            async with session.get(self._url) as response:
                async for line in response.content:
                    await self._queue.put(line)


async def _full_http3_request_task(url: str) -> str:
    proc = await asyncio.create_subprocess_exec(
        IntegrationTest.h3_request,
        f"--ca-certs={IntegrationTest.ca_certs}",
        url,
        stdout=asyncio.subprocess.PIPE,
    )
    (stdout, _) = await proc.communicate()
    await proc.wait()
    return stdout.decode("utf-8")


def _full_http3_request(url: str) -> Awaitable[str]:
    return asyncio.create_task(_full_http3_request_task(url))


async def _full_http_request_task(url: str) -> str:
    # Separate session per request is against aiohttp idioms, but is
    # intentional here because the point of the test is verifying
    # where connections go - reusing a connection would do the wrong thing.
    async with ClientSession() as session:
        async with session.get(url) as response:
            return await response.text()


def _full_http_request(url: str) -> Awaitable[str]:
    return asyncio.create_task(_full_http_request_task(url))


def filter_chains(codec_type: str = "AUTO") -> str:
    return f"""
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          codec_type: {codec_type}
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
"""


def _make_envoy_config_yaml(upstream_port: int, udp_upstream_port: int, file_path: pathlib.Path):
    file_path.write_text(
        f"""
admin:
  address:
    socket_address:
      address: {ENVOY_HOST}
      port_value: {ENVOY_ADMIN_PORT}

static_resources:
  listeners:
  - name: listener_udp
    address:
      socket_address:
        protocol: UDP
        address: {ENVOY_HOST}
        port_value: {ENVOY_UDP_PORT}
    listener_filters:
    - name: envoy.filters.udp_listener.udp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
        stat_prefix: udp_hotrestart
        matcher:
          on_no_match:
            action:
              name: route
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
                cluster: udp_service
  - name: listener_quic
    address:
      socket_address:
        protocol: UDP
        address: {ENVOY_HOST}
        port_value: {ENVOY_PORT}
{filter_chains("HTTP3")}
      transport_socket:
        name: "envoy.transport_sockets.quic"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport"
          downstream_tls_context:
            common_tls_context:
              tls_certificates:
              - certificate_chain:
                  filename: "{IntegrationTest.server_cert}"
                private_key:
                  filename: "{IntegrationTest.server_key}"
    udp_listener_config:
      quic_options: {"{}"}
      downstream_socket_config:
        prefer_gro: true
  - name: listener_http
    address:
      socket_address:
        address: {ENVOY_HOST}
        port_value: {ENVOY_PORT}
{filter_chains()}
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
  - name: udp_service
    type: STATIC
    load_assignment:
      cluster_name: udp_service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                protocol: UDP
                address: {UPSTREAM_HOST}
                port_value: {udp_upstream_port}
""")


class UdpUpstream(asyncio.DatagramProtocol):

    def __init__(self, tag: bytes):
        self.tag = tag

    def connection_made(self, transport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr) -> None:
        self.transport.sendto(data + self.tag, addr)


class UdpSession(asyncio.DatagramProtocol):

    @classmethod
    async def open(cls) -> Self:
        loop = asyncio.get_running_loop()
        _, protocol = await loop.create_datagram_endpoint(
            cls, remote_addr=(ENVOY_HOST, ENVOY_UDP_PORT))
        return protocol

    def __init__(self):
        self.responses: asyncio.Queue[bytes] = asyncio.Queue()

    def connection_made(self, transport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr) -> None:
        self.responses.put_nowait(data)

    async def request(self, payload: bytes) -> bytes:
        deadline = datetime.now() + timedelta(seconds=STARTUP_TOLERANCE_SECONDS)
        while datetime.now() < deadline:
            self.transport.sendto(payload)
            try:
                return await asyncio.wait_for(self.responses.get(), timeout=0.5)
            except asyncio.TimeoutError:
                pass
        raise TimeoutError(f"no udp response for {payload!r}")

    def close(self) -> None:
        self.transport.close()


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
    assert expected_substring in response, f"expected_substring={expected_substring}, server_info={response}"


async def _terminate_process(process: asyncio.subprocess.Process | None) -> None:
    if process is None or process.returncode is not None:
        return
    try:
        process.terminate()
    except ProcessLookupError:
        return
    try:
        await asyncio.wait_for(process.wait(), timeout=5)
    except asyncio.TimeoutError:
        process.kill()
        await process.wait()


class IntegrationTest(unittest.IsolatedAsyncioTestCase):
    server_cert: pathlib.Path
    server_key: pathlib.Path
    ca_certs: pathlib.Path
    h3_request: pathlib.Path
    envoy_binary: pathlib.Path

    async def asyncSetUp(self) -> None:
        print(os.environ)
        tmpdir = os.environ["TEST_TMPDIR"]
        self.slow_config_path = pathlib.Path(tmpdir, "slow_config.yaml")
        self.fast_config_path = pathlib.Path(tmpdir, "fast_config.yaml")
        self.base_id_path = pathlib.Path(tmpdir, "base_id.txt")
        _make_envoy_config_yaml(
            upstream_port=UPSTREAM_SLOW_PORT,
            udp_upstream_port=UPSTREAM_UDP_SLOW_PORT,
            file_path=self.slow_config_path)
        _make_envoy_config_yaml(
            upstream_port=UPSTREAM_FAST_PORT,
            udp_upstream_port=UPSTREAM_UDP_FAST_PORT,
            file_path=self.fast_config_path)
        self.base_envoy_args = [
            IntegrationTest.envoy_binary,
            "--socket-path",
            SOCKET_PATH,
            "--socket-mode",
            str(SOCKET_MODE),
        ]
        log.info(f"starting upstreams on https://{ENVOY_HOST}:{ENVOY_PORT}/")
        await super().asyncSetUp()
        self.slow_upstream = Upstream()
        await self.slow_upstream.start()
        self.fast_upstream = Upstream(True)
        await self.fast_upstream.start()
        loop = asyncio.get_running_loop()
        self.slow_udp_transport, _ = await loop.create_datagram_endpoint(
            lambda: UdpUpstream(b" via-slow"), local_addr=(UPSTREAM_HOST, UPSTREAM_UDP_SLOW_PORT))
        self.fast_udp_transport, _ = await loop.create_datagram_endpoint(
            lambda: UdpUpstream(b" via-fast"), local_addr=(UPSTREAM_HOST, UPSTREAM_UDP_FAST_PORT))

    async def asyncTearDown(self) -> None:
        self.slow_udp_transport.close()
        self.fast_udp_transport.close()
        await self.slow_upstream.stop()
        await self.fast_upstream.stop()
        return await super().asyncTearDown()

    async def _start_envoy(
            self, stack: contextlib.AsyncExitStack, *args: str) -> asyncio.subprocess.Process:
        process = await asyncio.create_subprocess_exec(*self.base_envoy_args, *args)
        stack.push_async_callback(_terminate_process, process)
        return process

    async def _open_udp_session(self, stack: contextlib.AsyncExitStack) -> UdpSession:
        session = await UdpSession.open()
        stack.callback(session.close)
        return session

    async def _start_parent_envoy(self, stack: contextlib.AsyncExitStack) -> asyncio.subprocess.Process:
        log.info("starting envoy")
        process = await self._start_envoy(
            stack,
            "--restart-epoch",
            "0",
            "--use-dynamic-base-id",
            "--base-id-path",
            self.base_id_path,
            "-c",
            self.slow_config_path,
        )
        log.info(f"cert path = {IntegrationTest.server_cert}")
        log.info("waiting for envoy ready")
        await _wait_for_envoy_epoch(0)
        return process

    async def _terminate_parent_envoy(self, process: asyncio.subprocess.Process) -> None:
        log.info("terminating first envoy process")
        process.terminate()
        await process.wait()

    async def _start_child_envoy_without_parent(
            self, stack: contextlib.AsyncExitStack, base_id: int) -> asyncio.subprocess.Process:
        log.info("starting envoy with hot restart config but parent is dead")
        process = await self._start_envoy(
            stack,
            "--restart-epoch",
            "1",
            "--base-id",
            str(base_id),
            "--skip-hot-restart-on-no-parent",
            "-c",
            self.fast_config_path,
        )
        log.info("waiting for envoy ready")
        await _wait_for_envoy_epoch(1)
        return process

    async def _start_slow_requests(self) -> list[LineGenerator]:
        log.info("making requests")
        slow_responses = [HttpRequestLineGenerator(ENVOY_URL) for _ in range(PARALLEL_REQUESTS)] + [
            Http3RequestLineGenerator(ENVOY_HTTPS_URL) for _ in range(PARALLEL_REQUESTS)
        ]
        log.info("waiting for responses to begin")
        for response in slow_responses:
            self.assertEqual(await response.line(), b"start\n")
        return slow_responses

    async def _open_udp_session_via_slow(self, stack: contextlib.AsyncExitStack) -> UdpSession:
        log.info("establishing udp session")
        udp_session = await self._open_udp_session(stack)
        response = await udp_session.request(b"hello")
        self.assertTrue(response.endswith(b" via-slow"), response)
        return udp_session

    async def _start_hot_restart_child(
            self, stack: contextlib.AsyncExitStack, base_id: int) -> asyncio.subprocess.Process:
        log.info(f"starting envoy hot restart for base id {base_id}")
        process = await self._start_envoy(
            stack,
            "--restart-epoch",
            "1",
            "--parent-shutdown-time-s",
            str(STARTUP_TOLERANCE_SECONDS * 2),
            "--base-id",
            str(base_id),
            "-c",
            self.fast_config_path,
        )
        log.info("waiting for new envoy instance to begin")
        await _wait_for_envoy_epoch(1)
        return process

    async def _assert_new_requests_go_to_fast(
            self, assertion_message: str, request_log: str = "sending request to fast upstream") -> None:
        log.info(request_log)
        fast_responses = ([_full_http_request(ENVOY_URL) for _ in range(PARALLEL_REQUESTS)] +
                          [_full_http3_request(ENVOY_HTTPS_URL) for _ in range(PARALLEL_REQUESTS)])
        for response in fast_responses:
            self.assertEqual(await response, "fast instance", assertion_message)

    async def _assert_udp_sessions_during_drain(
            self,
            stack: contextlib.AsyncExitStack,
            udp_session_a: UdpSession) -> UdpSession:
        log.info("checking udp sessions during drain")
        udp_session_b = await self._open_udp_session(stack)
        response = await udp_session_a.request(b"hello")
        self.assertTrue(response.endswith(b" via-slow"), response)
        response = await udp_session_b.request(b"hello")
        self.assertTrue(response.endswith(b" via-fast"), response)
        return udp_session_b

    async def _assert_slow_requests_complete_on_old_instance(
            self, slow_responses: list[LineGenerator]) -> None:
        log.info("releasing original slow request")
        self.slow_upstream.release.set()
        for response in slow_responses:
            self.assertEqual(await response.line(), b"end\n")
        for response in slow_responses:
            self.assertEqual(await response.join(), 0)

    async def _assert_udp_sessions_after_drain(
            self, udp_session_a: UdpSession, udp_session_b: UdpSession) -> None:
        response = await udp_session_b.request(b"hello")
        self.assertTrue(response.endswith(b" via-fast"), response)
        response = await udp_session_a.request(b"hello")
        self.assertTrue(response.endswith(b" via-fast"), response)

    async def test_dead_parent_startup(self) -> None:
        async with contextlib.AsyncExitStack() as stack:
            await self._test_dead_parent_startup(stack)

    async def _test_dead_parent_startup(self, stack: contextlib.AsyncExitStack) -> None:
        envoy_process_1 = await self._start_parent_envoy(stack)
        base_id = int(self.base_id_path.read_text())
        await self._terminate_parent_envoy(envoy_process_1)
        await self._start_child_envoy_without_parent(stack, base_id)
        await self._assert_new_requests_go_to_fast(
            "envoy server should be running despite failed hot restart")

    async def test_connection_handoffs(self) -> None:
        async with contextlib.AsyncExitStack() as stack:
            stack.callback(self.slow_upstream.release.set)
            await self._test_connection_handoffs(stack)

    async def _test_connection_handoffs(self, stack: contextlib.AsyncExitStack) -> None:
        envoy_process_1 = await self._start_parent_envoy(stack)
        slow_responses = await self._start_slow_requests()
        udp_session_a = await self._open_udp_session_via_slow(stack)
        base_id = int(self.base_id_path.read_text())
        await self._start_hot_restart_child(stack, base_id)
        await self._assert_new_requests_go_to_fast(
            "new requests after hot restart begins should go to new cluster")
        udp_session_b = await self._assert_udp_sessions_during_drain(stack, udp_session_a)
        await self._assert_slow_requests_complete_on_old_instance(slow_responses)
        log.info("waiting for parent instance to terminate")
        await envoy_process_1.wait()
        await self._assert_new_requests_go_to_fast(
            "new requests after old instance terminates should go to new cluster",
            request_log="sending second request to fast upstream",
        )
        await self._assert_udp_sessions_after_drain(udp_session_a, udp_session_b)


def generate_server_cert(
        ca_key_path: pathlib.Path,
        ca_cert_path: pathlib.Path) -> "tuple[pathlib.Path, pathlib.Path]":
    """Generates a temporary key and cert pem file and returns the paths.

    This is necessary because the http3 client validates that the server
    certificate matches the host of the request, and our host is an
    arbitrary randomized 127.x.y.z IP address to reduce the likelihood
    of port collisions during testing. We therefore must use a generated
    certificate that really matches the host IP.
    """

    from cryptography import x509
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.hazmat.backends import default_backend
    from cryptography.hazmat.primitives import serialization
    from ipaddress import ip_address

    with open(ca_key_path, "rb") as ca_key_file:
        ca_key = serialization.load_pem_private_key(
            ca_key_file.read(),
            password=None,
        )
    with open(ca_cert_path, "rb") as ca_cert_file:
        ca_cert = x509.load_pem_x509_certificate(ca_cert_file.read())

    key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
        backend=default_backend(),
    )

    hostname = "testhost"
    name = x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, hostname)])
    alt_names = [x509.DNSName(hostname)]
    alt_names.append(x509.IPAddress(ip_address(ENVOY_HOST)))
    san = x509.SubjectAlternativeName(alt_names)
    basic_constraints = x509.BasicConstraints(ca=True, path_length=0)
    now = datetime.now(timezone.utc).replace(tzinfo=None)
    cert = (
        x509.CertificateBuilder()  # Comment to keep linter from uglifying!
        .subject_name(name).issuer_name(ca_cert.subject).public_key(key.public_key()).serial_number(
            1).not_valid_before(now).not_valid_after(now + timedelta(days=30)).add_extension(
                basic_constraints,
                False).add_extension(san, False).sign(ca_key, hashes.SHA256(), default_backend()))
    cert_pem = cert.public_bytes(encoding=serialization.Encoding.PEM)
    key_pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    )
    cert_file = tempfile.NamedTemporaryFile(
        suffix="_key.pem", delete=False, dir=os.environ["TEST_TMPDIR"])
    cert_file.write(cert_pem)
    cert_file.close()
    key_file = tempfile.NamedTemporaryFile(
        suffix="_cert.pem", delete=False, dir=os.environ["TEST_TMPDIR"])
    key_file.write(key_pem)
    key_file.close()
    return key_file.name, cert_file.name


def main():
    parser = argparse.ArgumentParser(description="Hot restart handoff test")
    parser.add_argument("--envoy-binary", type=str, required=True)
    parser.add_argument("--h3-request", type=str, required=True)
    parser.add_argument("--ca-certs", type=str, required=True)
    parser.add_argument("--ca-key", type=str, required=True)
    # unittest also parses some args, so we strip out the ones we're using
    # and leave the rest for unittest to consume.
    (args, sys.argv[1:]) = parser.parse_known_args()
    (IntegrationTest.server_key,
     IntegrationTest.server_cert) = generate_server_cert(args.ca_key, args.ca_certs)
    IntegrationTest.ca_certs = args.ca_certs
    IntegrationTest.h3_request = args.h3_request
    IntegrationTest.envoy_binary = args.envoy_binary

    unittest.main()


if __name__ == "__main__":
    main()
