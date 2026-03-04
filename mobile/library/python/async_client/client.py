"""High level asyncio client built on top of the Envoy Mobile bindings."""

import asyncio
from typing import Any, Dict, List, Optional, Union

import library.python.envoy_engine as envoy_engine
from library.python.envoy_engine import EngineBuilder

from .executor import AsyncioExecutor, Executor
from .response import Response
from .utils import (
    normalize_request,
)


class StreamCallbacks:
    """Container for a single-stream's callback handlers."""

    def __init__(
        self, response: Response, stream_complete: asyncio.Event, executor: Executor
    ) -> None:
        self._response = response
        self._stream_complete = stream_complete
        self._executor = executor

    def attach(self, engine: envoy_engine.Engine) -> envoy_engine.Stream:
        proto = engine.stream_client().new_stream_prototype()
        return proto.start(
            on_headers=self._executor.wrap(self.on_headers),
            on_data=self._executor.wrap(self.on_data),
            on_trailers=self._executor.wrap(self.on_trailers),
            on_complete=self._executor.wrap(self.on_complete),
            on_error=self._executor.wrap(self.on_error),
            on_cancel=self._executor.wrap(self.on_cancel),
            explicit_flow_control=False,
        )

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
                self._response.status_code = int(status[0] if isinstance(status, list) else status)
            except ValueError:
                pass
        for key, value in headers.items():
            self._response.headers[key] = (
                value[0] if isinstance(value, list) and len(value) == 1 else value
            )

    def on_data(
        self,
        data: bytes,
        length: int,
        end_stream: bool,
        intel: envoy_engine.StreamIntel,
    ) -> None:
        # length is redundant with len(data)
        self._response.body_raw.extend(data)

    def on_trailers(
        self,
        trailers: Dict[str, Union[str, List[str]]],
        intel: envoy_engine.StreamIntel,
    ) -> None:
        for key, value in trailers.items():
            self._response.trailers[key] = (
                value[0] if isinstance(value, list) and len(value) == 1 else value
            )

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
        self._response.envoy_error = error
        self._stream_complete.set()

    def on_cancel(
        self, intel: envoy_engine.StreamIntel, final_intel: envoy_engine.FinalStreamIntel
    ) -> None:
        self._stream_complete.set()


class AsyncClient:
    """A very small HTTP client that speaks the subset of envoy_requests
    that we care about.

    Each client instance owns its own Envoy engine and executor. The
    ``request()`` method returns a :class:`Response` object once the stream
    has completed; the operation itself is fully non-blocking thanks to the
    underlying ``asyncio`` event loop and the ``AsyncioExecutor``.

    Use as an async context manager: ``async with AsyncClient(engine_builder) as client:``
    """

    def __init__(self, engine_builder: EngineBuilder) -> None:
        """Construct a new AsyncClient.

        Args:
            engine_builder: A pre-configured EngineBuilder to finalize and build.
        """
        self._engine_builder = engine_builder
        self._engine = None
        self._executor = None
        self._engine_running = None

    async def __aenter__(self) -> "AsyncClient":
        """Enter the async context manager, initialize the engine."""
        self._engine_running = asyncio.Event()
        self._executor = AsyncioExecutor(loop=asyncio.get_running_loop())

        # Finalize the engine builder with the engine-running callback and build
        self._engine = self._engine_builder.set_on_engine_running(
            self._executor.wrap(self._engine_running.set)
        ).build()

        # Wait for the engine to be running
        await self._engine_running.wait()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        """Exit the async context manager, terminate the engine."""
        if self._engine is not None:
            self._engine.terminate()
            self._engine = None

    def __del__(self) -> None:
        """Clean up the engine on destruction."""
        if hasattr(self, "_engine") and self._engine is not None:
            self._engine.terminate()

    async def request(
        self,
        method: str,
        url: str,
        *,
        data: Any = None,
        headers: Dict[str, Union[str, List[str]]] = None,
        timeout: Optional[Union[int, float]] = None,
    ) -> Response:
        """Send a single request and wait for the response."""
        response = Response()
        stream_complete = asyncio.Event()

        callbacks = StreamCallbacks(response, stream_complete, self._executor)
        stream = callbacks.attach(self._engine)
        self._send_request(stream, method, url, data=data, headers=headers, timeout=timeout)
        await stream_complete.wait()
        return response

    def _send_request(
        self,
        stream: envoy_engine.Stream,
        method: str,
        url: str,
        *,
        data: Any = None,
        headers: Dict[str, Union[str, List[str]]] = None,
        timeout: Optional[Union[int, float]] = None,
    ) -> None:
        # Normalize the request and get headers and body
        header_dict, body = normalize_request(method, url, data=data, headers=headers, timeout=timeout)

        # Send headers with end_stream flag based on whether we have a body
        has_data = len(body) > 0
        stream.send_headers(header_dict, not has_data)
        if has_data:
            stream.close(body)

    # convenience helpers for HTTP verbs
    async def delete(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("DELETE", url, **kwargs)

    async def get(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("GET", url, **kwargs)

    async def head(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("HEAD", url, **kwargs)

    async def options(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("OPTIONS", url, **kwargs)

    async def patch(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("PATCH", url, **kwargs)

    async def post(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("POST", url, **kwargs)

    async def put(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("PUT", url, **kwargs)

    async def trace(self, url: str, **kwargs) -> Response:  # type: ignore[no-untyped-def]
        return await self.request("TRACE", url, **kwargs)
