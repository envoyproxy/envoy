"""Integration tests for connection pool draining in Envoy Mobile Python transports."""

import asyncio
import threading
import unittest
from unittest import mock
from test.python.echo_test_server import EchoTestServer

from envoy_mobile import (
    EngineBuilder,
    EnvoyClientTransport,
    AsyncEnvoyClientTransport,
    LogLevel,
)
import httpx


class HttpxTransportFetchTest(unittest.TestCase):
    """Tests verifying targeted connection pool draining on transport close."""

    @classmethod
    def setUpClass(cls):
        """Set up an echo test server for the tests to hit."""
        cls._echo_server = EchoTestServer()
        cls._echo_server.start()
        cls._echo_server_url = f"http://{cls._echo_server.url}"

    @classmethod
    def tearDownClass(cls):
        """Shut down the echo test server."""
        cls._echo_server.stop()

    def _build_engine(self):
        """Helper to build and start an engine with stats and socket tagging enabled."""
        engine_running = threading.Event()
        engine = (
            EngineBuilder()
            .set_log_level(LogLevel.info)
            .enable_stats_collection(True)
            .enable_socket_tagging(True)
            .add_runtime_guard("dns_cache_set_ip_version_to_remove", True)
            .add_runtime_guard("getaddrinfo_no_ai_flags", True)
            .set_on_engine_running(lambda: engine_running.set())
            .enable_worker_thread(True)
            .build()
        )
        self.assertTrue(engine_running.wait(timeout=30), "Engine did not start within timeout")
        return engine

    def test_basic_request_with_close(self):
        """Verify that closing a synchronous transport drains its isolated connections."""
        engine = self._build_engine()
        transport = EnvoyClientTransport(engine)
        mock_engine = mock.MagicMock(wraps=engine)
        transport._engine = mock_engine
        client = httpx.Client(transport=transport)

        # Make a successful request to establish an upstream connection
        response = client.get(self._echo_server_url)
        self.assertEqual(response.status_code, 200)

        # Close the transport (this triggers drain_connections_by_socket_tag)
        client.close()
        mock_engine.drain_connections_by_socket_tag.assert_called_once_with(
            transport._transport_tag
        )

        engine.terminate()

    def test_async_basic_request_with_close(self):
        """Verify that async closing an async transport drains its isolated connections."""
        engine = self._build_engine()

        async def run_request():
            transport = AsyncEnvoyClientTransport(engine)
            mock_engine = mock.MagicMock(wraps=engine)
            transport._engine = mock_engine
            client = httpx.AsyncClient(transport=transport)
            response = await client.get(self._echo_server_url)
            self.assertEqual(response.status_code, 200)

            await client.aclose()
            mock_engine.drain_connections_by_socket_tag.assert_called_once_with(
                transport._transport_tag
            )

        asyncio.run(run_request())
        engine.terminate()


if __name__ == "__main__":
    unittest.main()
