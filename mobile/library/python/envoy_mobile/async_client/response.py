"""Lightweight container for data received from an Envoy stream."""

import asyncio
import json
from typing import Any, Dict, List, Optional, Union

from ..envoy_engine import EnvoyError
from .executor import Executor
from .. import envoy_engine


# ClientResponseError is a catch-all for any error that occurs during the request/response lifecycle
class ClientResponseError(Exception):
    def __init__(
        self,
        message: str,
        envoy_error: Optional[EnvoyError] = None,
        status: Optional[int] = None,
    ) -> None:
        super().__init__(message)
        self.envoy_error = envoy_error
        self.status = status


# A Response object is created for each request and populated by the stream callbacks as data is received.
class Response:
    _READ_SIZE = 1024

    def __init__(
        self, header_complete: asyncio.Event, stream_complete: asyncio.Event, executor: Executor
    ) -> None:
        self.__body_raw = bytearray()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}
        self.envoy_error: Optional[EnvoyError] = None
        self.__stream: Optional[envoy_engine.Stream] = None
        self.__header_complete = header_complete
        self.__stream_complete = stream_complete
        self.__more_response_data_received = asyncio.Event()
        self.__executor = executor
        self.__eof_received = False
        self.__cached_body: Optional[bytes] = None
        self.__streaming_started = False
        self.__stream_reader: Optional["StreamReader"] = None

    def attach(self, engine: envoy_engine.Engine) -> envoy_engine.Stream:
        proto = engine.stream_client().new_stream_prototype()
        self.__stream = proto.start(
            on_headers=self.__executor.wrap(self.on_headers),
            on_data=self.__executor.wrap(self.on_data),
            on_trailers=self.__executor.wrap(self.on_trailers),
            on_complete=self.__executor.wrap(self.on_complete),
            on_error=self.__executor.wrap(self.on_error),
            on_cancel=self.__executor.wrap(self.on_cancel),
            explicit_flow_control=True,
        )
        return self.__stream

    async def __aenter__(self) -> "Response":
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def close(self) -> None:
        """Close the response and cancel the underlying stream if it's still active."""
        # If the stream is not yet complete, cancel it to free resources.
        if self.__stream is not None and not self.__stream_complete.is_set():
            self.__stream.cancel()

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
            self.__eof_received = True
        self.__header_complete.set()

    def on_data(
        self,
        data: bytes,
        length: int,
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        # length is redundant with len(data)
        assert length == len(data)
        self.__body_raw.extend(data)
        if end_stream:
            self.__eof_received = True
        self.__more_response_data_received.set()

    def on_trailers(
        self,
        trailers: Dict[str, Union[str, List[str]]],
        intel: envoy_engine.StreamIntel,
    ) -> None:
        for key, value in trailers.items():
            self.trailers[key] = value[0] if isinstance(value, list) and len(value) == 1 else value
        self.__eof_received = True

    def on_complete(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        self.__stream_complete.set()

    def on_error(
        self,
        error: envoy_engine.EnvoyError,
        intel: envoy_engine.StreamIntel,
        final_intel: envoy_engine.FinalStreamIntel,
    ) -> None:
        self.envoy_error = error
        self.__stream_complete.set()

    def on_cancel(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        self.__stream_complete.set()

    async def read(self) -> bytes:
        """Read the full response body.

        If streaming has already started via content.read(), this returns empty bytes.
        Otherwise, it reads the whole stream, caches the result, and returns it.
        Subsequent calls return the cached body.
        """
        if self.__streaming_started:
            return b""
        if self.__cached_body is not None:
            return self.__cached_body

        self.__cached_body = await self.__read_all_stream()
        return self.__cached_body

    async def __read_stream_chunk(self, n: int) -> bytes:
        """Helper method to read up to n bytes where n > 0."""
        assert n > 0
        self.__stream.read_data(n)
        await asyncio.wait(
            [
                asyncio.create_task(self.__more_response_data_received.wait()),
                asyncio.create_task(self.__stream_complete.wait()),
            ],
            return_when=asyncio.FIRST_COMPLETED,
        )
        if self.__stream_complete.is_set() and not self.__eof_received:
            raise ClientResponseError(
                "Request failed or canceled before response was fully received", self.envoy_error
            )
        # Clear the per-read states
        self.__more_response_data_received.clear()
        if len(self.__body_raw) == 0:
            return b""
        # Received bytes should be not greater than n.
        assert len(self.__body_raw) <= n
        more_bytes = bytes(self.__body_raw)
        self.__body_raw = bytearray()
        return more_bytes

    async def __read_all_stream(self) -> bytes:
        body = bytearray()
        while True:
            chunk = await self.__read_stream_chunk(self._READ_SIZE)
            if not chunk:
                break
            body.extend(chunk)
        return bytes(body)

    @property
    def ok(self) -> bool:
        return self.status_code is not None and self.status_code < 400

    def raise_for_status(self) -> None:
        if not self.ok:
            raise ClientResponseError(
                f"Response error: {self.status_code}", status=self.status_code
            )

    @property
    def content(self) -> "StreamReader":
        """return a streaming interface to fetch the response body. It allows the user to fetch part of the body via read(n) iteratively without having to buffer the whole body in memory. This is useful for large responses."""
        if self.__stream_reader is None:
            self.__stream_reader = StreamReader(self)
        return self.__stream_reader

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
        if self._response._Response__cached_body is not None:
            # If the full body has already been read and cached, return EOF.
            return b""

        self._response._Response__streaming_started = True

        if n > 0:
            return await self._response._Response__read_stream_chunk(n)
        if n == 0:
            return b""

        # read the entire stream until EOF
        return await self._response._Response__read_all_stream()
