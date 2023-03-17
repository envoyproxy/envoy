import asyncio
import functools
from typing import Any
from typing import Callable
from typing import cast
from typing import TypeVar

from .common.core import make_stream
from .common.core import send_request
from .common.engine import Engine
from .common.executor import Executor
from .response import Response


async def pre_build_engine() -> None:
    engine_running = asyncio.Event()
    executor = AsyncioExecutor()
    Engine.handle(executor, lambda: engine_running.set())
    await engine_running.wait()


# TODO: add better typing to this (and functions that use it)
async def request(*args, **kwargs) -> Response:
    response = Response()
    engine_running = asyncio.Event()
    stream_complete = asyncio.Event()
    executor = AsyncioExecutor()

    engine = Engine.handle(executor, lambda: engine_running.set())
    await engine_running.wait()

    stream = make_stream(
        engine,
        executor,
        response,
        lambda: stream_complete.set(),
    )
    send_request(stream, *args, **kwargs)
    await stream_complete.wait()
    return response


async def delete(*args, **kwargs) -> Response:
    return await request("delete", *args, **kwargs)


async def get(*args, **kwargs) -> Response:
    return await request("get", *args, **kwargs)


async def head(*args, **kwargs) -> Response:
    return await request("head", *args, **kwargs)


async def options(*args, **kwargs) -> Response:
    return await request("options", *args, **kwargs)


async def patch(*args, **kwargs) -> Response:
    return await request("patch", *args, **kwargs)


async def post(*args, **kwargs) -> Response:
    return await request("post", *args, **kwargs)


async def put(*args, **kwargs) -> Response:
    return await request("put", *args, **kwargs)


async def trace(*args, **kwargs) -> Response:
    return await request("trace", *args, **kwargs)


Func = TypeVar("Func", bound=Callable[..., Any])


class AsyncioExecutor(Executor):

    def __init__(self):
        self.loop = asyncio.get_running_loop()

    def wrap(self, fn: Func) -> Func:

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            self.loop.call_soon_threadsafe(fn, *args, **kwargs)

        return cast(Func, wrapper)
