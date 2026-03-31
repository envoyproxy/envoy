"""Factory for creating Envoy Mobile httpx transports with a shared engine."""

import threading
from typing import Optional

from . import envoy_engine
from .async_client_transport import AsyncEnvoyClientTransport
from .sync_client_transport import EnvoyClientTransport


class EnvoyTransportFactory:
    """Factory for managing a single, shared Envoy Engine.

    Envoy Engines are expensive to initialize and should typically live for the
    entire life of the process. This factory ensures that only one Engine is
    created and shared across all httpx transports.
    """

    _engine: Optional[envoy_engine.Engine] = None
    _lock = threading.Lock()

    @classmethod
    def get_shared_engine(
        cls, builder: Optional[envoy_engine.EngineBuilder] = None
    ) -> envoy_engine.Engine:
        """Get the shared Envoy Engine instance, initializing it if necessary.

        Args:
            builder: An optional EngineBuilder to configure the engine before
                it is started. This is only used if the engine hasn't been
                initialized yet.

        Returns:
            The shared envoy_engine.Engine instance.
        """
        if cls._engine is not None:
            return cls._engine

        with cls._lock:
            if cls._engine is None:
                # Use default builder if none provided
                if builder is None:
                    builder = envoy_engine.EngineBuilder()

                # Build and start the engine
                cls._engine = builder.build()

            return cls._engine

    @classmethod
    def get_async_transport(
        cls, builder: Optional[envoy_engine.EngineBuilder] = None
    ) -> AsyncEnvoyClientTransport:
        """Create an AsyncEnvoyClientTransport using the shared engine.

        Args:
            builder: Optional EngineBuilder for engine initialization.

        Returns:
            An AsyncEnvoyClientTransport instance.
        """
        engine = cls.get_shared_engine(builder)
        return AsyncEnvoyClientTransport(engine)

    @classmethod
    def get_sync_transport(
        cls, builder: Optional[envoy_engine.EngineBuilder] = None
    ) -> EnvoyClientTransport:
        """Create an EnvoyClientTransport using the shared engine.

        Args:
            builder: Optional EngineBuilder for engine initialization.

        Returns:
            An EnvoyClientTransport instance.
        """
        engine = cls.get_shared_engine(builder)
        return EnvoyClientTransport(engine)
