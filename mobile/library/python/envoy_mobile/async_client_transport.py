"""Async httpx transport for Envoy Mobile."""

import asyncio
from typing import AsyncIterable, Dict, List, Optional, Union

import httpx
from . import envoy_engine
from .async_client.executor import AsyncioExecutor
from .httpx_utils import get_envoy_headers, map_envoy_error


class AsyncEnvoyStream(httpx.AsyncByteStream):
    """An asynchronous byte stream that reads data from an Envoy stream."""

    def __init__(
        self,
        stream: envoy_engine.Stream,
        queue: asyncio.Queue,
        stream_complete: asyncio.Event,
        executor: AsyncioExecutor,
    ) -> None:
        self._stream = stream
        self._queue = queue
        self._stream_complete = stream_complete
        self._executor = executor
        self._closed = False

    async def __aiter__(self) -> AsyncIterable[bytes]:
        try:
            while True:
                # Use explicit flow control to request more data from Envoy.
                # We request a chunk and then wait for the queue to populate.
                if not self._stream_complete.is_set():
                    # Request up to 64KB at a time
                    self._stream.read_data(65536)

                # Wait for data or completion
                item = await self._queue.get()
                if item is None:  # EOF
                    break
                if isinstance(item, Exception):
                    raise item

                yield item
        finally:
            await self.aclose()

    async def aclose(self) -> None:
        if not self._closed:
            if not self._stream_complete.is_set():
                self._stream.cancel()
            self._closed = True


class AsyncResponseHandler:
    """Handles callbacks from the Envoy engine and pushes data to asyncio queues."""

    def __init__(self, executor: AsyncioExecutor) -> None:
        self.executor = executor
        self.headers_future: asyncio.Future = asyncio.Future()
        self.data_queue: asyncio.Queue = asyncio.Queue()
        self.stream_complete = asyncio.Event()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}

    def on_headers(
        self,
        headers: Dict[str, Union[str, List[str]]],
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        status = headers.get(":status")
        if status is not None:
            try:
                self.status_code = int(status[0] if isinstance(status, list) else status)
            except (ValueError, IndexError):
                pass

        for key, value in headers.items():
            if not key.startswith(":"):
                self.headers[key] = (
                    value[0] if isinstance(value, list) and len(value) == 1 else value
                )

        if not self.headers_future.done():
            self.headers_future.set_result(True)

        if end_stream:
            self.data_queue.put_nowait(None)
            self.stream_complete.set()

    def on_data(
        self,
        data: bytes,
        length: int,
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        self.data_queue.put_nowait(data)
        if end_stream:
            self.data_queue.put_nowait(None)

    def on_trailers(
        self,
        trailers: Dict[str, Union[str, List[str]]],
        intel: envoy_engine.StreamIntel,
    ) -> None:
        for key, value in trailers.items():
            self.trailers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value
        self.data_queue.put_nowait(None)

    def on_complete(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        if not self.stream_complete.is_set():
            self.data_queue.put_nowait(None)
            self.stream_complete.set()

    def on_error(
        self,
        error: envoy_engine.EnvoyError,
        intel: envoy_engine.StreamIntel,
        final_intel: envoy_engine.FinalStreamIntel,
    ) -> None:
        exc = map_envoy_error(error.error_code, error.message)
        if not self.headers_future.done():
            self.headers_future.set_exception(exc)
        self.data_queue.put_nowait(exc)
        self.stream_complete.set()

    def on_cancel(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        exc = httpx.RequestError("Request cancelled")
        if not self.headers_future.done():
            self.headers_future.set_exception(exc)
        self.data_queue.put_nowait(exc)
        self.stream_complete.set()


class AsyncEnvoyClientTransport(httpx.AsyncBaseTransport):
    """An asynchronous transport for httpx that uses Envoy Mobile."""

    def __init__(self, engine: envoy_engine.Engine) -> None:
        self._engine = engine
        self._executor = AsyncioExecutor()

    async def handle_async_request(self, request: httpx.Request) -> httpx.Response:
        # Map headers
        timeout = request.extensions.get("timeout", {}).get("read")
        envoy_headers = get_envoy_headers(request, timeout=timeout)

        # Create handler
        handler = AsyncResponseHandler(self._executor)

        # Start stream
        proto = self._engine.stream_client().new_stream_prototype()
        stream = proto.start(
            on_headers=self._executor.wrap(handler.on_headers),
            on_data=self._executor.wrap(handler.on_data),
            on_trailers=self._executor.wrap(handler.on_trailers),
            on_complete=self._executor.wrap(handler.on_complete),
            on_error=self._executor.wrap(handler.on_error),
            on_cancel=self._executor.wrap(handler.on_cancel),
            explicit_flow_control=True,
        )

        # --- Send Request ---
        #
        # In httpx, the request body is accessed via `request.stream`, which provides
        # an asynchronous iterator over the body chunks. This is crucial for:
        # 1. Memory Efficiency: We don't load the entire body into memory, which
        #    is essential for large file uploads.
        # 2. Support for Generators: If the user provides a generator as the
        #    request content, we consume it one chunk at a time.
        #
        # Simplified Request Logic:
        # 1. Always send headers first with `end_stream=False`.
        # 2. Stream all chunks from `request.stream` with `end_stream=False`.
        # 3. Finalize by sending an empty string with `end_stream=True` (via `stream.close(b"")`).

        # Start by sending the request headers.
        # Note: These stream operations are synchronous in Python because they are
        # non-blocking calls that interface directly with the Envoy Mobile C++ engine.
        stream.send_headers(envoy_headers, False)

        # Iterate through the request stream and send all data chunks.
        async for chunk in request.stream:
            stream.send_data(chunk, False)

        # Finalize the request. Sending an empty string with `stream.close()`
        # signals `end_stream=True` to Envoy, completing the request side of the stream.
        stream.close(b"")

        # Wait for headers
        try:
            await handler.headers_future
        except Exception:
            stream.cancel()
            raise

        return httpx.Response(
            status_code=handler.status_code or 0,
            headers=handler.headers,
            stream=AsyncEnvoyStream(
                stream, handler.data_queue, handler.stream_complete, self._executor
            ),
        )
