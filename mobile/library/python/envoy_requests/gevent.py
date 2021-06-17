import functools
import threading
from typing import Any
from typing import Callable
from typing import Dict
from typing import Generic
from typing import List
from typing import Tuple
from typing import TypeVar
from typing import cast

import gevent
from gevent.event import Event
from gevent.pool import Group

from .common.core import make_stream
from .common.core import send_request
from .common.engine import Engine
from .common.executor import Executor
from .response import Response


def pre_build_engine() -> None:
    engine_running = Event()
    executor = GeventExecutor()
    Engine.handle(executor, lambda: engine_running.set())
    engine_running.wait()


# TODO: add better typing to this (and functions that use it)
def request(*args, **kwargs) -> Response:
    response = Response()
    engine_running = Event()
    stream_complete = Event()
    executor = GeventExecutor()

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


T = TypeVar("T")
Func = TypeVar("Func", bound=Callable[..., Any])


class GeventExecutor(Executor):
    def __init__(self):
        self.group = Group()
        self.channel: GeventChannel[
            Tuple[Callable, List[Any], Dict[str, Any]]
        ] = GeventChannel()
        self.spawn_work_greenlet = gevent.spawn(self._spawn_work)

    def __del__(self):
        self.spawn_work_greenlet.kill()

    def wrap(self, fn: Func) -> Func:
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            self.channel.put((fn, args, kwargs))

        return cast(Func, wrapper)

    def _spawn_work(self):
        while True:
            (fn, args, kwargs) = self.channel.get()
            self.group.spawn(fn, *args, **kwargs)


class GeventChannel(Generic[T]):
    def __init__(self):
        self.hub = gevent.get_hub()
        self.watcher = self.hub.loop.async_()
        self.lock = threading.Lock()
        self.values: List[T] = []

    def put(self, value: T) -> None:
        with self.lock:
            self.values.append(value)
            self.watcher.send()

    def get(self) -> T:
        self.lock.acquire()
        while len(self.values) == 0:
            self.lock.release()
            self.hub.wait(self.watcher)
            self.lock.acquire()

        value: T = self.values.pop(0)
        self.lock.release()
        return value
