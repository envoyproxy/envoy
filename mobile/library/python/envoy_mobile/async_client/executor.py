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
    """Executor that schedules callbacks on an asyncio event loop."""

    def __init__(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> None:
        # Allow an explicit loop to be supplied (useful for testing).
        # If none is provided, attempt to grab the currently running loop.
        if loop is None:
            try:
                loop = asyncio.get_running_loop()
            except RuntimeError:
                # Fallback for when no loop is running in the current thread.
                # In such cases, the executor must be explicitly initialized
                # with the correct loop.
                pass
        self.loop = loop

    def wrap(self, fn: Func) -> Func:
        """Return a version of ``fn`` that is safe to call from any thread."""

        @functools.wraps(fn)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            if self.loop is None:
                # If we don't have a loop, we can't schedule the callback.
                # In a real-world scenario, this might happen if the loop
                # was closed or if it was never correctly initialized.
                raise RuntimeError("AsyncioExecutor must be initialized with a running loop.")

            if self.loop.is_closed():
                return None

            # ``call_soon_threadsafe`` will schedule the callable on the same
            # loop that constructed the executor, making every callback "safe".
            # Note: call_soon_threadsafe doesn't return the result of fn.
            self.loop.call_soon_threadsafe(fn, *args, **kwargs)
            return None

        return cast(Func, wrapper)
