import functools
import threading
from typing import Any
from typing import Callable
from typing import Dict
from typing import Generic
from typing import TypeVar

import gevent
from gevent.pool import Group

from library.python.envoy_engine import Engine
from library.python.envoy_engine import EngineBuilder
from library.python.envoy_engine import EnvoyError
from library.python.envoy_engine import ResponseHeaders
from library.python.envoy_engine import ResponseTrailers
from library.python.envoy_engine import StreamClient
from library.python.envoy_engine import StreamPrototype


T = TypeVar("T")


# we can't use normal inheritance here, because the classes backing these
# gevent flavored classes cannot be constructed from inside python except
# by their parent C++ class.
#
# instead we use composition, and use __getattr__ to automatically retrieve
# a base class's fields rather than manually writing out all of the properties
# and methods.
class CompositionalInheritor(Generic[T]):
    def __init__(self, base: T):
        self.base = base

    def __getattr__(self, name: str):
        return getattr(self.base, name)


# we have to define wrappers around everything from EngineBuilder -> StreamPrototype
# in order to wrap all callbacks in the GeventExecutor
class GeventEngineBuilder(EngineBuilder):
    def __init__(self):
        super().__init__()
        self.executor = GeventExecutor()

    def set_on_engine_running(self, closure: Callable[[], None]) -> EngineBuilder:
        super().set_on_engine_running(self.executor(closure))
        return self

    def build(self) -> "GeventEngine":
        return GeventEngine(super().build())


class GeventEngine(CompositionalInheritor[Engine]):
    def __init__(self, base: Engine):
        super().__init__(base)

    def stream_client(self) -> "GeventStreamClient":
        return GeventStreamClient(self.base.stream_client())


class GeventStreamClient(CompositionalInheritor[StreamClient]):
    def __init__(self, base: StreamClient):
        super().__init__(base)

    def new_stream_prototype(self) -> "GeventStreamPrototype":
        return GeventStreamPrototype(self.base.new_stream_prototype())


class GeventStreamPrototype(CompositionalInheritor[StreamPrototype]):
    def __init__(self, base: StreamClient):
        super().__init__(base)
        self.executor = GeventExecutor()

    def set_on_headers(self, closure: Callable[[ResponseHeaders, bool], None]) -> "GeventStreamPrototype":
        self.base.set_on_headers(self.executor(closure))
        return self

    def set_on_data(self, closure: Callable[[bytes, bool], None]) -> "GeventStreamPrototype":
        self.base.set_on_data(self.executor(closure))
        return self

    def set_on_trailers(self, closure: Callable[[ResponseTrailers, bool], None]) -> "GeventStreamPrototype":
        self.base.set_on_trailers(self.executor(closure))
        return self

    def set_on_complete(self, closure: Callable[[], None]) -> "GeventStreamPrototype":
        self.base.set_on_complete(self.executor(closure))
        return self

    def set_on_error(self, closure: Callable[[EnvoyError], None]) -> "GeventStreamPrototype":
        self.base.set_on_error(self.executor(closure))
        return self

    def set_on_cancel(self, closure: Callable[[], None]) -> "GeventStreamPrototype":
        self.base.set_on_cancel(self.executor(closure))
        return self


class GeventExecutor():
    def __init__(self):
        super().__init__()
        self.group = Group()
        self.channel: ThreadsafeChannel[Tuple[Callable, List[Any], Dict[str, Any]]] = ThreadsafeChannel()
        self.spawn_work_greenlet = gevent.spawn(self._spawn_work)

    def __del__(self):
        super().__del__()
        self.spawn_work_greenlet.kill()

    def __call__(self, fn: Callable) -> Callable:
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            self.channel.put((fn, args, kwargs))
        return wrapper

    def _spawn_work(self):
        while True:
            (fn, args, kwargs) = self.channel.get()
            self.group.spawn(fn, *args, **kwargs)


class ThreadsafeChannel(Generic[T]):
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
