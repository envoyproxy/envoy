import functools
from queue import Empty as QueueEmpty
from queue import Queue
from threading import Event
from threading import Lock
from threading import Thread
from typing import Any
from typing import Callable
from typing import TypeVar
from typing import cast

from .common.core import make_stream
from .common.core import send_request
from .common.engine import Engine
from .common.executor import Executor
from .response import Response


def pre_build_engine() -> None:
    engine_running = Event()
    executor = ThreadingExecutor()
    Engine.handle(executor, lambda: engine_running.set())
    engine_running.wait()


# TODO: add better typing to this (and functions that use it)
def request(*args, **kwargs) -> Response:
    response = Response()
    engine_running = Event()
    stream_complete = Event()
    executor = ThreadingExecutor()

    engine = Engine.handle(executor, lambda: engine_running.set())
    engine_running.wait()

    stream = make_stream(
        engine, executor, response, lambda: stream_complete.set(),
    )
    send_request(stream, *args, **kwargs)
    stream_complete.wait()
    return response


def delete(*args, **kwargs) -> Response:
    return request("delete", *args, **kwargs)


def get(*args, **kwargs) -> Response:
    return request("get", *args, **kwargs)


def head(*args, **kwargs) -> Response:
    return request("head", *args, **kwargs)


def options(*args, **kwargs) -> Response:
    return request("options", *args, **kwargs)


def patch(*args, **kwargs) -> Response:
    return request("patch", *args, **kwargs)


def post(*args, **kwargs) -> Response:
    return request("post", *args, **kwargs)


def put(*args, **kwargs) -> Response:
    return request("put", *args, **kwargs)


def trace(*args, **kwargs) -> Response:
    return request("trace", *args, **kwargs)


Func = TypeVar("Func", bound=Callable[..., Any])


class ThreadingExecutor(Executor):
    def __init__(self):
        self.lock = Lock()

    def wrap(self, fn: Func) -> Func:
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            with self.lock:
                fn(*args, **kwargs)

        return cast(Func, wrapper)
