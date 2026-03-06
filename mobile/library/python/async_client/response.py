"""Lightweight container for data received from an Envoy stream."""

import asyncio
import json
from typing import Any, Dict, List, Optional, Union

from library.python.envoy_engine import EnvoyError
from .executor import Executor
import library.python.envoy_engine as envoy_engine


# ClientResponseError is a catch-all for any error that occurs during the request/response lifecycle
class ClientResponseError(Exception):
    def __init__(self, message: str, envoy_error: Optional[EnvoyError] = None) -> None:
        super().__init__(message)
        self.envoy_error = envoy_error


# A Response object is created for each request and populated by the stream callbacks as data is received.
class Response:
    _READ_SIZE = 1024

    def __init__(
        self, header_complete: asyncio.Event, stream_complete: asyncio.Event, executor: Executor
    ) -> None:
        self.body_raw = bytearray()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}
        self.envoy_error: Optional[EnvoyError] = None
        self._stream: Optional[envoy_engine.Stream] = None
        self._header_complete = header_complete
        self._stream_complete = stream_complete
        self._more_response_data_received = asyncio.Event()
        self._executor = executor
        self._eof_received = False
        self._cached_body: Optional[bytes] = None
        self._streaming_started = False
        self._stream_reader: Optional["StreamReader"] = None

    def attach(self, engine: envoy_engine.Engine) -> envoy_engine.Stream:
        proto = engine.stream_client().new_stream_prototype()
        self._stream = proto.start(
            on_headers=self._executor.wrap(self.on_headers),
            on_data=self._executor.wrap(self.on_data),
            on_trailers=self._executor.wrap(self.on_trailers),
            on_complete=self._executor.wrap(self.on_complete),
            on_error=self._executor.wrap(self.on_error),
            on_cancel=self._executor.wrap(self.on_cancel),
            explicit_flow_control=True,
        )
        return self._stream

    async def __aenter__(self) -> "Response":
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def close(self) -> None:
        """Close the response and cancel the underlying stream if it's still active."""
        # If the stream is not yet complete, cancel it to free resources.
        if self._stream is not None and not self._stream_complete.is_set():
            self._stream.cancel()

    def on_headers(
        self,
        headers: Dict[str, Union[str, List[str]]],
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        # shim delivers a dict where values are lists of strings
        # status code arrives as the ":status" pseudo-header
        status = headers.get(":status")
        if status is not None:
            try:
                self.status_code = int(status[0] if isinstance(status, list) else status)
            except ValueError:
                pass
        for key, value in headers.items():
            self.headers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value
        if end_stream:
            self._eof_received = True
        self._header_complete.set()

    def on_data(
        self,
        data: bytes,
        length: int,
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        # length is redundant with len(data)
        assert length == len(data)
        self.body_raw.extend(data)
        if end_stream:
            self._eof_received = True
        self._more_response_data_received.set()

    def on_trailers(
        self,
        trailers: Dict[str, Union[str, List[str]]],
        intel: envoy_engine.StreamIntel,
    ) -> None:
        for key, value in trailers.items():
            self.trailers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value
        self._eof_received = True

    def on_complete(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        self._stream_complete.set()

    def on_error(
        self,
        error: envoy_engine.EnvoyError,
        intel: envoy_engine.StreamIntel,
        final_intel: envoy_engine.FinalStreamIntel,
    ) -> None:
        self.envoy_error = error
        self._stream_complete.set()

    def on_cancel(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        self._stream_complete.set()

    async def read(self) -> bytes:
        """Read the full response body.

        If streaming has already started via content.read(), this returns empty bytes.
        Otherwise, it reads the whole stream, caches the result, and returns it.
        Subsequent calls return the cached body.
        """
        if self._streaming_started:
            return b""
        if self._cached_body is not None:
            return self._cached_body

        self._cached_body = await self._read_all_stream()
        return self._cached_body

    async def _read_stream_chunk(self, n: int) -> bytes:
        """Helper method to read up to n bytes where n > 0."""
        assert n > 0
        self._stream.read_data(n)
        await asyncio.wait(
            [
                asyncio.create_task(self._more_response_data_received.wait()),
                asyncio.create_task(self._stream_complete.wait()),
            ],
            return_when=asyncio.FIRST_COMPLETED,
        )
        if self._stream_complete.is_set() and not self._eof_received:
            raise ClientResponseError(
                "Request failed or canceled before response was fully received", self.envoy_error
            )
        # Clear the per-read states
        self._more_response_data_received.clear()
        if len(self.body_raw) == 0:
            return b""
        # Received bytes should be not greater than n.
        assert len(self.body_raw) <= n
        more_bytes = bytes(self.body_raw)
        self.body_raw = bytearray()
        return more_bytes

    async def _read_all_stream(self) -> bytes:
        body = bytearray()
        while True:
            chunk = await self._read_stream_chunk(self._READ_SIZE)
            if not chunk:
                break
            body.extend(chunk)
        return bytes(body)

    @property
    def content(self) -> "StreamReader":
        """return a streaming interface to fetch the response body. It allows the user to fetch part of the body via read(n) iteratively without having to buffer the whole body in memory. This is useful for large responses."""
        if self._stream_reader is None:
            self._stream_reader = StreamReader(self)
        return self._stream_reader

    @property
    async def body(self) -> bytes:
        return await self.read()

    @property
    async def text(self) -> str:
        # TODO: respect charset from headers
        return str(await self.body, "utf8")

    async def json(self) -> Dict[str, Any]:
        return json.loads(await self.body)


class StreamReader:
    def __init__(self, response: Response) -> None:
        self._response = response

    async def read(self, n: int = -1) -> bytes:
        """Read up to n bytes. If n is negative, read the whole stream."""
        if self._response._cached_body is not None:
            # If the full body has already been read and cached, return EOF.
            return b""

        self._response._streaming_started = True

        if n > 0:
            return await self._response._read_stream_chunk(n)
        if n == 0:
            return b""

        # read the entire stream until EOF
        return await self._response._read_all_stream()
