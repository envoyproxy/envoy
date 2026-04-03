"""Executor abstractions used to marshal callbacks onto the asyncio loop."""

import asyncio
import functools
from typing import Any, Callable, TypeVar, cast, Optional

Func = TypeVar("Func", bound=Callable[..., Any])


class Executor:
    """Minimal interface used by engine/stream helper code."""

    def wrap(self, fn: Func) -> Func:
        """Return a version of ``fn`` that is safe to call from any thread."""
        # default implementation is a no-op; subclasses override it.
        return fn  # type: ignore[return-value]


class AsyncioExecutor(Executor):
    def __init__(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> None:
        # Allow an explicit loop to be supplied (useful for testing).
        # If none is provided, attempt to grab the currently running loop.
        if loop is None:
            loop = asyncio.get_running_loop()
        self.loop = loop

    def wrap(self, fn: Func) -> Func:
        @functools.wraps(fn)
        def wrapper(*args: Any, **kwargs: Any) -> None:  # type: ignore[return-value]
            # ``call_soon_threadsafe`` will schedule the callable on the same
            # loop that constructed the executor, making every callback "safe"
            self.loop.call_soon_threadsafe(fn, *args, **kwargs)

        return cast(Func, wrapper)
