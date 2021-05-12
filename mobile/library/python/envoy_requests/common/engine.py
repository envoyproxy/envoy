import atexit
from threading import Event
from typing import Optional

from envoy_engine import Engine as EnvoyEngine
from envoy_engine import EngineBuilder
from envoy_engine import LogLevel


class Engine:
    log_level: LogLevel = LogLevel.Error
    config_template_override: Optional[str] = None
    _handle: Optional[EnvoyEngine] = None

    @classmethod
    def build(cls):
        if cls.config_template_override is None:
            engine_builder = EngineBuilder()
        else:
            engine_builder = EngineBuilder(cls.config_template_override)

        engine_running = Event()
        cls._handle = (
            engine_builder.add_log_level(cls.log_level).set_on_engine_running(
                lambda: engine_running.set()
            )
        ).build()

        atexit.register(lambda: cls._handle.terminate())
        engine_running.wait()

    @classmethod
    def handle(cls):
        if cls._handle is None:
            cls.build()
        return cls._handle
