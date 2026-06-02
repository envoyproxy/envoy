"""Unit tests for Envoy Client httpx transports.

These tests use unittest.mock to simulate the Envoy Mobile C++ engine (envoy_engine).
We verify that the transports correctly map httpx requests to Envoy headers,
handle streaming request/response bodies, and propagate errors appropriately.
"""

import asyncio
import sys
import unittest
from unittest.mock import MagicMock, patch, call

# Mock envoy_engine before any imports that might use it.
# This is necessary because envoy_engine is a C++ extension module that
# may not be available in all test environments.
mock_envoy_engine = MagicMock()
# The code does `from .. import envoy_engine` from inside envoy_mobile.async_client_transport
# So it looks for `envoy_mobile.envoy_engine`
sys.modules["envoy_mobile.envoy_engine"] = mock_envoy_engine

import httpx
from envoy_mobile import (
    AsyncEnvoyClientTransport,
    EnvoyClientTransport,
)


# Define these for use in tests since we mocked the module.
class MockEnvoyError:
    def __init__(self):
        self.error_code = 0
        self.message = ""


mock_envoy_engine.EnvoyError = MockEnvoyError


class TestEnvoyClientTransport(unittest.TestCase):
    """Tests for the synchronous EnvoyClientTransport."""

    def setUp(self):
        self.mock_engine = MagicMock()
        self.mock_stream_client = MagicMock()
        self.mock_engine.stream_client.return_value = self.mock_stream_client
        self.mock_prototype = MagicMock()
        self.mock_stream_client.new_stream_prototype.return_value = self.mock_prototype
        self.mock_stream = MagicMock()
        self.mock_prototype.start.return_value = self.mock_stream
        self.transport = EnvoyClientTransport(self.mock_engine)

    def test_handle_request_success(self):
        """Verify a successful synchronous request/response cycle."""
        request = httpx.Request("GET", "https://example.com/path")

        def side_effect(*args, **kwargs):
            # Simulate Envoy receiving headers and data.
            on_headers = kwargs.get("on_headers")
            on_headers({":status": "200", "content-type": ["application/json"]}, False, None)
            on_data = kwargs.get("on_data")
            on_data(b'{"key": "value"}', 16, True, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        response = self.transport.handle_request(request)

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers["content-type"], "application/json")
        self.assertEqual(response.read(), b'{"key": "value"}')

        # Verify Envoy pseudo-headers were correctly mapped.
        # Note: In the simplified logic, headers are always sent with end_stream=False.
        self.mock_stream.send_headers.assert_called_once()
        sent_headers = self.mock_stream.send_headers.call_args[0][0]
        end_stream = self.mock_stream.send_headers.call_args[0][1]
        self.assertEqual(sent_headers[":method"], "GET")
        self.assertFalse(end_stream)

        # Verify the stream was closed with empty data.
        self.mock_stream.close.assert_called_once_with(b"")

    def test_handle_request_error(self):
        """Verify that Envoy errors are mapped to httpx exceptions."""
        request = httpx.Request("GET", "https://example.com")

        def side_effect(*args, **kwargs):
            on_error = kwargs.get("on_error")
            error = MockEnvoyError()
            error.error_code = 2  # ConnectionFailure
            error.message = "Connection failed"
            on_error(error, None, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        with self.assertRaises(httpx.ConnectError):
            self.transport.handle_request(request)

    def test_handle_request_streaming(self):
        """Verify that request bodies are streamed to Envoy chunk-by-chunk."""

        def stream_generator():
            yield b"chunk1"
            yield b"chunk2"

        request = httpx.Request("POST", "https://example.com/stream", content=stream_generator())

        def side_effect(*args, **kwargs):
            on_headers = kwargs.get("on_headers")
            on_headers({":status": "200"}, False, None)
            on_data = kwargs.get("on_data")
            on_data(b"ok", 2, True, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        response = self.transport.handle_request(request)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.read(), b"ok")

        # Verify call sequence: headers, chunk1, chunk2, close.
        self.mock_stream.send_headers.assert_called_once_with(unittest.mock.ANY, False)
        self.mock_stream.send_data.assert_has_calls(
            [
                call(b"chunk1", False),
                call(b"chunk2", False),
            ]
        )
        self.mock_stream.close.assert_called_once_with(b"")


class TestAsyncEnvoyClientTransport(unittest.IsolatedAsyncioTestCase):
    """Tests for the asynchronous AsyncEnvoyClientTransport."""

    async def asyncSetUp(self):
        self.mock_engine = MagicMock()
        self.mock_stream_client = MagicMock()
        self.mock_engine.stream_client.return_value = self.mock_stream_client
        self.mock_prototype = MagicMock()
        self.mock_stream_client.new_stream_prototype.return_value = self.mock_prototype
        self.mock_stream = MagicMock()
        self.mock_prototype.start.return_value = self.mock_stream
        self.transport = AsyncEnvoyClientTransport(self.mock_engine)

    async def test_handle_async_request_success(self):
        """Verify a successful asynchronous request/response cycle."""
        request = httpx.Request("POST", "https://example.com/post", content=b"body")

        def side_effect(*args, **kwargs):
            on_headers = kwargs.get("on_headers")
            # Callbacks must be scheduled on the event loop to simulate the AsyncioExecutor.
            loop = asyncio.get_running_loop()
            loop.call_soon(on_headers, {":status": "201"}, False, None)
            on_data = kwargs.get("on_data")
            loop.call_soon(on_data, b"created", 7, True, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        response = await self.transport.handle_async_request(request)

        self.assertEqual(response.status_code, 201)
        self.assertEqual(await response.aread(), b"created")

        self.mock_stream.send_headers.assert_called_once_with(unittest.mock.ANY, False)
        self.mock_stream.send_data.assert_called_once_with(b"body", False)
        self.mock_stream.close.assert_called_with(b"")

    async def test_handle_async_request_error(self):
        """Verify that Envoy errors are mapped to httpx exceptions asynchronously."""
        request = httpx.Request("GET", "https://example.com")

        def side_effect(*args, **kwargs):
            on_error = kwargs.get("on_error")
            error = MockEnvoyError()
            error.error_code = 4  # RequestTimeout
            error.message = "Timed out"
            loop = asyncio.get_running_loop()
            loop.call_soon(on_error, error, None, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        with self.assertRaises(httpx.ReadTimeout):
            await self.transport.handle_async_request(request)

    async def test_handle_async_request_streaming(self):
        """Verify asynchronous request body streaming."""

        async def stream_generator():
            yield b"chunk1"
            yield b"chunk2"

        request = httpx.Request("POST", "https://example.com/stream", content=stream_generator())

        def side_effect(*args, **kwargs):
            on_headers = kwargs.get("on_headers")
            loop = asyncio.get_running_loop()
            loop.call_soon(on_headers, {":status": "200"}, False, None)
            on_data = kwargs.get("on_data")
            loop.call_soon(on_data, b"ok", 2, True, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        response = await self.transport.handle_async_request(request)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(await response.aread(), b"ok")

        self.mock_stream.send_headers.assert_called_once_with(unittest.mock.ANY, False)
        self.mock_stream.send_data.assert_has_calls(
            [
                call(b"chunk1", False),
                call(b"chunk2", False),
            ]
        )
        self.mock_stream.close.assert_called_once_with(b"")


from envoy_mobile.async_client.executor import AsyncioExecutor


class TestAsyncioExecutor(unittest.TestCase):
    """Tests for the AsyncioExecutor thread-safety wrapper."""

    def test_strict_failure_mode(self):
        """Ensure that the executor fails if used without an active loop."""
        # Instantiate without a running event loop.
        executor = AsyncioExecutor(loop=None)

        def my_func():
            pass

        wrapped = executor.wrap(my_func)
        # Invoking the wrapped function should raise RuntimeError to protect thread-safety.
        with self.assertRaises(RuntimeError):
            wrapped()


if __name__ == "__main__":
    unittest.main()
