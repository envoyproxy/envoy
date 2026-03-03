"""High level asyncio client built on top of the Envoy Mobile bindings."""

import asyncio
from typing import Any, Dict, List, Optional, Union
from urllib.parse import urlparse

import library.python.envoy_engine as envoy_engine
from library.python.envoy_engine import EngineBuilder

from .executor import AsyncioExecutor, Executor
from .response import Response
from .utils import (
    normalize_data,
    normalize_headers,
    normalize_method,
    normalize_timeout_to_ms,
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
    """

    def __init__(
        self, engine_builder: EngineBuilder, *, _loop: Optional[asyncio.AbstractEventLoop] = None
    ) -> None:
        """Construct a new AsyncClient.

        This constructor is intentionally guarded: callers should prefer the
        async factory ``AsyncClient.create(...)`` which guarantees construction
        from within a running asyncio event loop. Direct synchronous
        construction will raise a helpful error.

        Args:
            engine_builder: A pre-configured EngineBuilder to finalize and build.
            _loop: Internal/testing: the event loop to use. If omitted,
                   construction will fail to force usage of ``create``.
        """
        if _loop is None:
            raise RuntimeError(
                "AsyncClient must be created via `await AsyncClient.create(engine_builder)` "
                "so that it can capture the running asyncio event loop."
            )

        self._engine_running = asyncio.Event()
        self._executor = AsyncioExecutor(loop=_loop)

        # Finalize the engine builder with the engine-running callback and build
        self._engine = engine_builder.set_on_engine_running(
            self._executor.wrap(self._engine_running.set)
        ).build()

    @classmethod
    async def create(cls, engine_builder: EngineBuilder) -> "AsyncClient":
        """Async factory that constructs an AsyncClient inside the running loop."""
        loop = asyncio.get_running_loop()
        # call the guarded constructor with the running loop
        return cls(engine_builder, _loop=loop)

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
        """Send a single request and wait for the response.

        Waits for the engine to be running before proceeding.
        """
        # Wait for engine to be ready at the very beginning
        await self._engine_running.wait()

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
        # normalize pieces that go into the header map
        norm_method = normalize_method(method)
        norm_data, data_headers = normalize_data(data)
        norm_headers = {**data_headers, **normalize_headers(headers)}
        norm_timeout_ms = normalize_timeout_to_ms(timeout)

        parsed = urlparse(url)
        header_dict: Dict[str, Union[str, List[str]]] = {
            ":method": norm_method,
            ":scheme": parsed.scheme,
            ":authority": parsed.netloc,
            ":path": parsed.path,
        }
        if norm_timeout_ms > 0:
            header_dict["x-envoy-upstream-rq-timeout-ms"] = str(norm_timeout_ms)
        for key, values in norm_headers.items():
            header_dict[key] = values if len(values) > 1 else values[0]

        has_data = len(norm_data) > 0
        stream.send_headers(header_dict, not has_data)
        if has_data:
            stream.close(norm_data)

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
