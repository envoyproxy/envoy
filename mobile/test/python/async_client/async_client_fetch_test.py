"""Integration tests for the Envoy Mobile Python asyncio bindings."""

import asyncio
import json
import random
import unittest
from test.python.echo_test_server import EchoTestServer

from envoy_mobile import EngineBuilder, LogLevel, AsyncClient
from envoy_mobile.async_client.response import ClientResponseError


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

    def _make_client_builder(self):
        # factory to keep constructor logic consistent without sharing instances
        builder = EngineBuilder().set_log_level(LogLevel.trace)
        return builder

    def test_simple_get_request(self):
        """Send a simple GET request and verify we receive a response."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.get(f"{self._echo_server_url}/") as response:
                    self.assertEqual(response.status_code, 200)
                    self.assertTrue(response.ok)
                    self.assertGreater(len(await response.body), 0)

        asyncio.run(run())

    def test_get_with_custom_headers_and_streaming_response(self):
        """Send a GET request with custom headers."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                headers = {"x-custom-header": "test-value"}
                async with await client.get(
                    f"{self._echo_server_url}/",
                    headers=headers,
                ) as response:
                    self.assertEqual(response.status_code, 200)
                    body = bytearray()
                    while True:
                        chunk = await response.content.read(1)  # Read 1 bytes at a time.
                        if not chunk:
                            break  # EOF
                        body.extend(chunk)
                    self.assertIn(
                        "x-custom-header", json.loads(body)["headers"]
                    )  # Verify the body contains the custom header we sent.

        asyncio.run(run())

    def test_post_request_with_data(self):
        """Send a POST request with body data."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                test_data = "hello from async client"
                async with await client.post(
                    f"{self._echo_server_url}/",
                    data=test_data,
                ) as response:
                    self.assertEqual(response.status_code, 200)
                    # Echo server should echo back the data
                    self.assertIn(test_data, await response.text)

        asyncio.run(run())

    def test_head_request(self):
        """Send a HEAD request."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.head(f"{self._echo_server_url}/") as response:
                    self.assertEqual(response.status_code, 200)
                    # HEAD response should have no body
                    self.assertEqual(len(await response.body), 0)

        asyncio.run(run())

    def test_multiple_requests_same_client(self):
        """Send multiple requests using the same client concurrently."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
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
                    async with resp:
                        self.assertEqual(resp.status_code, 200)

        asyncio.run(run())

    def test_delete_request(self):
        """Send a DELETE request."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.delete(f"{self._echo_server_url}/") as response:
                    self.assertEqual(response.status_code, 200)

        asyncio.run(run())

    def test_put_request(self):
        """Send a PUT request with data."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.put(
                    f"{self._echo_server_url}/",
                    data="updated content",
                ) as response:
                    self.assertEqual(response.status_code, 200)

        asyncio.run(run())

    def test_patch_request(self):
        """Send a PATCH request with data."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.patch(
                    f"{self._echo_server_url}/",
                    data="patched content",
                ) as response:
                    self.assertEqual(response.status_code, 200)

        asyncio.run(run())

    def test_response_headers(self):
        """Verify response headers are properly parsed."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.get(f"{self._echo_server_url}/") as response:
                    self.assertEqual(response.status_code, 200)
                    # Verify status code header is present
                    self.assertIn(":status", response.headers)

        asyncio.run(run())

    def test_response_text_property(self):
        """Verify response text property decodes body as UTF-8."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.post(
                    f"{self._echo_server_url}/",
                    data="test string ñ",
                ) as response:
                    self.assertEqual(response.status_code, 200)
                    # Verify text property works
                    text = await response.text
                    self.assertIsInstance(text, str)
                    self.assertGreater(len(text), 0)

        asyncio.run(run())

    def test_raise_for_status(self):
        """Verify ok property and raise_for_status with 400 response."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                async with await client.get(f"{self._echo_server_url}/notfound") as response:
                    self.assertFalse(response.ok)
                    with self.assertRaises(ClientResponseError) as cm:
                        response.raise_for_status()
                    self.assertEqual(cm.exception.status, response.status_code)

        asyncio.run(run())

    def test_json_request(self):
        """Send a POST request with JSON data."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                payload = {"foo": "bar", "baz": 123}
                async with await client.post(
                    f"{self._echo_server_url}/",
                    json=payload,
                ) as response:
                    self.assertEqual(response.status_code, 200)
                    resp_json = await response.json()
                    # Echo server returns the body it received in the 'body' field
                    self.assertEqual(json.loads(resp_json["body"]), payload)
                    self.assertEqual(resp_json["headers"]["content-type"], "application/json")

        asyncio.run(run())

    def test_json_and_data_conflict(self):
        """Verify ValueError when both json and data are supplied."""

        async def run():
            async with AsyncClient(self._make_client_builder()) as client:
                with self.assertRaises(ValueError):
                    await client.post(
                        f"{self._echo_server_url}/",
                        json={"foo": "bar"},
                        data="some data",
                    )

        asyncio.run(run())


if __name__ == "__main__":
    unittest.main()
