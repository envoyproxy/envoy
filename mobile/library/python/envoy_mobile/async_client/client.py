"""High level asyncio client built on top of the Envoy Mobile bindings."""

import asyncio
from typing import Any, Dict, List, Optional, Union

from .. import envoy_engine
from ..envoy_engine import EngineBuilder

from .executor import AsyncioExecutor, Executor
from .response import ClientResponseError, Response
from .utils import (
    normalize_request,
)


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
        self._executor: Optional[Executor] = None
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

    # The main request method, which all verb-specific methods delegate to.
    async def request(self, method: str, url: str, **kwargs) -> Response:
        """Send a single request and wait for either the response headers to arrive or an error to occur.
        It returns the Response object populated with response headers if no error occurs, and the caller can await response.body to get the response body once it's fully received.
        If an error occurs before the headers are received, it raises a ClientResponseError with the underlying Envoy error attached.
        """
        stream_complete = asyncio.Event()
        header_complete = asyncio.Event()
        response = Response(header_complete, stream_complete, self._executor)
        stream = response.attach(self._engine)
        self._send_request(stream, method, url, **kwargs)
        # Wait for either the response headers to arrive or an error to occur.
        await asyncio.wait(
            [
                asyncio.create_task(stream_complete.wait()),
                asyncio.create_task(header_complete.wait()),
            ],
            return_when=asyncio.FIRST_COMPLETED,
        )
        if response.envoy_error is not None:
            raise ClientResponseError("Request failed with Envoy error", response.envoy_error)
        return response

    def _send_request(
        self,
        stream: envoy_engine.Stream,
        method: str,
        url: str,
        *,
        json: Any = None,
        data: Any = None,
        headers: Dict[str, Union[str, List[str]]] = None,
        timeout: Optional[Union[int, float]] = None,
    ) -> None:
        # Normalize the request and get headers and body
        header_dict, body = normalize_request(
            method, url, json=json, data=data, headers=headers, timeout=timeout
        )

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
