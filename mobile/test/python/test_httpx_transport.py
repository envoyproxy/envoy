"""Unit tests for Envoy Client httpx transports."""

import asyncio
import sys
import unittest
from unittest.mock import MagicMock, patch

# Mock envoy_engine before any imports that might use it
mock_envoy_engine = MagicMock()
# The code does `from .. import envoy_engine` from inside envoy_mobile.async_client_transport
# So it looks for `envoy_mobile.envoy_engine`
sys.modules["envoy_mobile.envoy_engine"] = mock_envoy_engine

import httpx
from envoy_mobile import (
    AsyncEnvoyClientTransport,
    EnvoyClientTransport,
)

# Define these for use in tests since we mocked the module
class MockEnvoyError:
    def __init__(self):
        self.error_code = 0
        self.message = ""

mock_envoy_engine.EnvoyError = MockEnvoyError


class TestEnvoyClientTransport(unittest.TestCase):
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
        request = httpx.Request("GET", "https://example.com/path")

        def side_effect(*args, **kwargs):
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

        # Verify Envoy headers
        self.mock_stream.send_headers.assert_called_once()
        sent_headers = self.mock_stream.send_headers.call_args[0][0]
        self.assertEqual(sent_headers[":method"], "GET")
        self.assertEqual(sent_headers[":authority"], "example.com")
        self.assertEqual(sent_headers[":path"], "/path")
        self.assertEqual(sent_headers[":scheme"], "https")

    def test_handle_request_error(self):
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

        self.mock_stream.send_headers.assert_called_once()
        self.mock_stream.send_data.assert_called_once_with(b"chunk1", False)
        self.mock_stream.close.assert_called_once_with(b"chunk2")


class TestAsyncEnvoyClientTransport(unittest.IsolatedAsyncioTestCase):
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
        request = httpx.Request("POST", "https://example.com/post", content=b"body")

        def side_effect(*args, **kwargs):
            on_headers = kwargs.get("on_headers")
            # We need to call this via the executor's loop
            loop = asyncio.get_running_loop()
            loop.call_soon(on_headers, {":status": "201"}, False, None)
            on_data = kwargs.get("on_data")
            loop.call_soon(on_data, b"created", 7, True, None)
            return self.mock_stream

        self.mock_prototype.start.side_effect = side_effect

        response = await self.transport.handle_async_request(request)

        self.assertEqual(response.status_code, 201)
        self.assertEqual(await response.aread(), b"created")

        # Verify Envoy headers
        self.mock_stream.send_headers.assert_called_once()
        sent_headers = self.mock_stream.send_headers.call_args[0][0]
        self.assertEqual(sent_headers[":method"], "POST")
        self.mock_stream.close.assert_called_with(b"body")

    async def test_handle_async_request_error(self):
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

        self.mock_stream.send_headers.assert_called_once()
        self.mock_stream.send_data.assert_called_once_with(b"chunk1", False)
        self.mock_stream.close.assert_called_once_with(b"chunk2")


from envoy_mobile.async_client.executor import AsyncioExecutor

class TestAsyncioExecutor(unittest.TestCase):
    def test_strict_failure_mode(self):
        # Instantiate without a running event loop
        executor = AsyncioExecutor(loop=None)
        
        def my_func():
            pass

        wrapped = executor.wrap(my_func)
        with self.assertRaises(RuntimeError):
            wrapped()

if __name__ == "__main__":
    unittest.main()
