"""Integration tests for the Envoy Mobile Python asyncio bindings."""

import asyncio
import random
import unittest
from test.python.echo_test_server import EchoTestServer

from library.python.envoy_engine import EngineBuilder, LogLevel
from library.python.async_client.client import AsyncClient


class TestAsyncClientFetch(unittest.TestCase):
    """Tests for making HTTP requests through AsyncClient."""

    @classmethod
    def setUpClass(cls):
        """Set up an echo test server for the tests to hit."""
        port = random.randint(2**14, 2**16)
        cls._echo_server = EchoTestServer("127.0.0.1", port)
        cls._echo_server.start()
        cls._echo_server_url = f"http://127.0.0.1:{port}"

    @classmethod
    def tearDownClass(cls):
        """Shut down the echo test server."""
        cls._echo_server.stop()

    async def _make_client(self):
        # factory to keep constructor logic consistent without sharing instances
        # AsyncClient constructor expects a running event loop, so this helper
        # is async and should be awaited from inside the test's event loop.
        builder = EngineBuilder().set_log_level(LogLevel.trace)
        client = await AsyncClient.create(builder)
        return client

    def test_simple_get_request(self):
        """Send a simple GET request and verify we receive a response."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.get(f"{self._echo_server_url}/")
                self.assertEqual(response.status_code, 200)
                self.assertGreater(len(response.body), 0)
            finally:
                del client

        asyncio.run(run())

    def test_get_with_custom_headers(self):
        """Send a GET request with custom headers."""

        async def run():
            client = await self._make_client()
            try:
                headers = {"x-custom-header": "test-value"}
                response = await client.get(
                    f"{self._echo_server_url}/",
                    headers=headers,
                )
                self.assertEqual(response.status_code, 200)
                # Echo server should echo back the custom header in the response body
                self.assertIn("x-custom-header", response.json()["headers"])
            finally:
                del client

        asyncio.run(run())

    def test_post_request_with_data(self):
        """Send a POST request with body data."""

        async def run():
            client = await self._make_client()
            try:
                test_data = "hello from async client"
                response = await client.post(
                    f"{self._echo_server_url}/",
                    data=test_data,
                )
                self.assertEqual(response.status_code, 200)
                # Echo server should echo back the data
                self.assertIn(test_data, response.text)
            finally:
                del client

        asyncio.run(run())

    def test_head_request(self):
        """Send a HEAD request."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.head(f"{self._echo_server_url}/")
                self.assertEqual(response.status_code, 200)
                # HEAD response should have no body
                self.assertEqual(len(response.body), 0)
            finally:
                del client

        asyncio.run(run())

    def test_multiple_requests_same_client(self):
        """Send multiple requests using the same client concurrently."""

        async def run():
            client = await self._make_client()
            try:
                tasks = [
                    client.get(f"{self._echo_server_url}/"),
                    client.post(
                        f"{self._echo_server_url}/",
                        data="second request",
                    ),
                    client.get(f"{self._echo_server_url}/"),
                ]
                responses = await asyncio.gather(*tasks)

                for resp in responses:
                    self.assertEqual(resp.status_code, 200)
            finally:
                del client

        asyncio.run(run())

    def test_delete_request(self):
        """Send a DELETE request."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.delete(f"{self._echo_server_url}/")
                self.assertEqual(response.status_code, 200)
            finally:
                del client

        asyncio.run(run())

    def test_put_request(self):
        """Send a PUT request with data."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.put(
                    f"{self._echo_server_url}/",
                    data="updated content",
                )
                self.assertEqual(response.status_code, 200)
            finally:
                del client

        asyncio.run(run())

    def test_patch_request(self):
        """Send a PATCH request with data."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.patch(
                    f"{self._echo_server_url}/",
                    data="patched content",
                )
                self.assertEqual(response.status_code, 200)
            finally:
                del client

        asyncio.run(run())

    def test_response_headers(self):
        """Verify response headers are properly parsed."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.get(f"{self._echo_server_url}/")
                self.assertEqual(response.status_code, 200)
                # Verify status code header is present
                self.assertIn(":status", response.headers)
            finally:
                del client

        asyncio.run(run())

    def test_response_text_property(self):
        """Verify response text property decodes body as UTF-8."""

        async def run():
            client = await self._make_client()
            try:
                response = await client.post(
                    f"{self._echo_server_url}/",
                    data="test string ñ",
                )
                self.assertEqual(response.status_code, 200)
                # Verify text property works
                text = response.text
                self.assertIsInstance(text, str)
                self.assertGreater(len(text), 0)
            finally:
                del client

        asyncio.run(run())


if __name__ == "__main__":
    unittest.main()
