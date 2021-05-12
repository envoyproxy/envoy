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


# TODO: add better typing to this (and functions that use it)
def request(*args, **kwargs) -> Response:
    response = Response()
    stream_complete = Event()
    executor = ThreadingExecutor()

    def _set_stream_complete():
        stream_complete.set()
        executor.finish()

    stream = make_stream(
        Engine.handle(), executor, response, _set_stream_complete,
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
        self.queue = Queue()
        self.process_callbacks = Thread(target=self._process_callbacks)
        self.process_callbacks.start()

    def wrap(self, fn: Func) -> Func:
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            self.queue.put((fn, args, kwargs))

        return cast(Func, wrapper)

    def finish(self):
        self.queue.put((None, None, None))

    def _process_callbacks(self):
        while True:
            try:
                fn, args, kwargs = self.queue.get(block=True, timeout=0.25)
            except QueueEmpty:
                continue
            if fn is None and args is None and kwargs is None:
                break
            with self.lock:
                fn(*args, **kwargs)
