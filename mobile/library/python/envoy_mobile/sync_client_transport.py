"""Synchronous httpx transport for Envoy Mobile."""

import queue
import threading
from typing import Dict, Iterable, List, Optional, Union

import httpx
from . import envoy_engine
from .httpx_utils import get_envoy_headers, map_envoy_error


class SyncEnvoyStream(httpx.SyncByteStream):
    def __init__(
        self,
        stream: envoy_engine.Stream,
        data_queue: queue.Queue,
        stream_complete: threading.Event,
    ) -> None:
        self._stream = stream
        self._queue = data_queue
        self._stream_complete = stream_complete
        self._closed = False

    def __iter__(self) -> Iterable[bytes]:
        try:
            while True:
                # Use explicit flow control to request more data from Envoy.
                if not self._stream_complete.is_set():
                    # Request up to 64KB at a time
                    self._stream.read_data(65536)

                # Wait for data or completion
                # Blocking wait for data. Terminal states push None or Exception to the queue.
                item = self._queue.get()

                if item is None:  # EOF
                    break
                if isinstance(item, Exception):
                    raise item

                yield item
        finally:
            self.close()

    def close(self) -> None:
        if not self._closed:
            if not self._stream_complete.is_set():
                self._stream.cancel()
            self._closed = True


class SyncResponseHandler:
    def __init__(self) -> None:
        self.headers_event = threading.Event()
        self.data_queue: queue.Queue = queue.Queue()
        self.stream_complete = threading.Event()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}
        self.exception: Optional[Exception] = None

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
                self.headers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value

        self.headers_event.set()

        if end_stream:
            self.data_queue.put(None)
            self.stream_complete.set()

    def on_data(
        self,
        data: bytes,
        length: int,
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        self.data_queue.put(data)
        if end_stream:
            self.data_queue.put(None)

    def on_trailers(
        self,
        trailers: Dict[str, Union[str, List[str]]],
        intel: envoy_engine.StreamIntel,
    ) -> None:
        for key, value in trailers.items():
            self.trailers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value
        self.data_queue.put(None)

    def on_complete(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        if not self.stream_complete.is_set():
            self.data_queue.put(None)
            self.stream_complete.set()

    def on_error(
        self,
        error: envoy_engine.EnvoyError,
        intel: envoy_engine.StreamIntel,
        final_intel: envoy_engine.FinalStreamIntel,
    ) -> None:
        exc = map_envoy_error(error.error_code, error.message)
        self.exception = exc
        self.data_queue.put(exc)
        self.headers_event.set()
        self.stream_complete.set()

    def on_cancel(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        exc = httpx.RequestError("Request cancelled")
        self.exception = exc
        self.data_queue.put(exc)
        self.headers_event.set()
        self.stream_complete.set()


class EnvoyClientTransport(httpx.BaseTransport):
    def __init__(self, engine: envoy_engine.Engine) -> None:
        self._engine = engine

    def handle_request(self, request: httpx.Request) -> httpx.Response:
        # Map headers
        timeout = request.extensions.get("timeout", {}).get("read")
        envoy_headers = get_envoy_headers(request, timeout=timeout)

        # Create handler
        handler = SyncResponseHandler()

        # Start stream
        proto = self._engine.stream_client().new_stream_prototype()
        stream = proto.start(
            on_headers=handler.on_headers,
            on_data=handler.on_data,
            on_trailers=handler.on_trailers,
            on_complete=handler.on_complete,
            on_error=handler.on_error,
            on_cancel=handler.on_cancel,
            explicit_flow_control=True,
        )

        # --- Send Request ---
        #
        # In httpx, the request body is accessed via `request.stream`, which provides
        # an iterator over the body chunks. This is crucial for:
        # 1. Memory Efficiency: We don't load the entire body into memory, which
        #    is essential for large file uploads.
        # 2. Support for Generators: If the user provides a generator as the
        #    request content, we consume it one chunk at a time.
        #
        # Envoy's stream API requires us to signal the 'end_stream' on either
        # `send_headers` (if there's no body) or `send_data/close` (if there is).
        previous_chunk = None
        has_body = False
        for chunk in request.stream:
            if not has_body:
                # We've found the first chunk, so we know a body exists.
                # We now send the headers with `end_stream=False`.
                has_body = True
                previous_chunk = chunk
                stream.send_headers(envoy_headers, False)
            else:
                # We've found a subsequent chunk. We send the *previous* chunk
                # now, knowing it's not the last one (`end_stream=False`).
                # We stay one chunk behind so we can correctly identify the
                # absolute final chunk for the `stream.close()` call.
                stream.send_data(previous_chunk, False)
                previous_chunk = chunk

        if has_body:
            # We've reached the end of the stream. We send the final chunk
            # using `stream.close()`, which signals `end_stream=True` to Envoy.
            stream.close(previous_chunk)
        else:
            # The `request.stream` was empty, meaning there is no body.
            # We send headers immediately with `end_stream=True`.
            stream.send_headers(envoy_headers, True)

        # Wait for headers
        handler.headers_event.wait()
        if handler.exception:
            stream.cancel()
            raise handler.exception

        return httpx.Response(
            status_code=handler.status_code or 0,
            headers=handler.headers,
            stream=SyncEnvoyStream(stream, handler.data_queue, handler.stream_complete),
        )
