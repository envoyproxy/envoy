import atexit
from typing import Callable
from typing import Optional

from envoy_engine import Engine as EnvoyEngine
from envoy_engine import EngineBuilder
from envoy_engine import LogLevel

from .executor import Executor


class Engine:
    log_level: LogLevel = LogLevel.Error
    config_template_override: Optional[str] = None
    _handle: Optional[EnvoyEngine] = None

    @classmethod
    def build(cls, executor: Executor, set_engine_running: Callable[[], None]):
        if cls.config_template_override is None:
            engine_builder = EngineBuilder()
        else:
            engine_builder = EngineBuilder(cls.config_template_override)

        cls._handle = (
            engine_builder.add_log_level(cls.log_level).set_on_engine_running(
                executor.wrap(set_engine_running))).build()
        atexit.register(lambda: cls._handle.terminate())

    @classmethod
    def handle(cls, executor: Executor, set_engine_running: Callable[[], None]) -> EnvoyEngine:
        if cls._handle is None:
            cls.build(executor, set_engine_running)
        else:
            executor.wrap(set_engine_running)()
        return cls._handle
