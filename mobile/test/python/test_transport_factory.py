"""Unit tests for EnvoyTransportFactory."""

import sys
import unittest
from unittest.mock import MagicMock, patch

# Mock envoy_engine before any imports that might use it
mock_envoy_engine = MagicMock()
sys.modules["envoy_mobile.envoy_engine"] = mock_envoy_engine

from envoy_mobile import (
    EnvoyTransportFactory,
    AsyncEnvoyClientTransport,
    EnvoyClientTransport,
)


class TestEnvoyTransportFactory(unittest.TestCase):
    def setUp(self):
        # Reset the shared engine state before each test
        EnvoyTransportFactory._engine = None
        mock_envoy_engine.reset_mock()

    def _setup_mock_builder(self, mock_builder, mock_engine, auto_fire_callback=True):
        """Helper to configure a mock builder.

        By default, it simulates the real engine by immediately invoking the
        registered 'on_engine_running' callback when build() is called.
        """
        running_callback = None

        def set_on_engine_running(callback):
            nonlocal running_callback
            running_callback = callback
            return mock_builder

        def build():
            if auto_fire_callback and running_callback:
                running_callback()
            return mock_engine

        mock_builder.set_on_engine_running.side_effect = set_on_engine_running
        mock_builder.build.side_effect = build

    def test_get_shared_engine_singleton(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()
        self._setup_mock_builder(mock_builder, mock_engine)

        # Call factory multiple times
        engine1 = EnvoyTransportFactory.get_shared_engine(mock_builder)
        engine2 = EnvoyTransportFactory.get_shared_engine(mock_builder)

        # Verify same instance returned
        self.assertEqual(engine1, engine2)
        self.assertEqual(engine1, mock_engine)

        # Verify build was only called once
        mock_builder.build.assert_called_once()

    def test_get_transports_use_shared_engine(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()
        self._setup_mock_builder(mock_builder, mock_engine)

        # Get transports
        async_transport = EnvoyTransportFactory.get_async_transport(mock_builder)
        sync_transport = EnvoyTransportFactory.get_sync_transport(mock_builder)

        # Verify they use the same engine instance
        self.assertEqual(async_transport._engine, mock_engine)
        self.assertEqual(sync_transport._engine, mock_engine)

        # Verify build was only called once
        mock_builder.build.assert_called_once()

    def test_default_builder_initialization(self):
        # Setup mocks
        mock_builder_instance = MagicMock()
        mock_envoy_engine.EngineBuilder.return_value = mock_builder_instance
        mock_engine = MagicMock()
        self._setup_mock_builder(mock_builder_instance, mock_engine)

        # Call without explicit builder
        engine = EnvoyTransportFactory.get_shared_engine()

        # Verify default builder was used
        mock_envoy_engine.EngineBuilder.assert_called_once()
        self.assertEqual(engine, mock_engine)

    def test_get_shared_engine_timeout(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()
        # Disable auto-fire of callback
        self._setup_mock_builder(mock_builder, mock_engine, auto_fire_callback=False)

        # Patch threading.Event.wait to return False immediately, simulating a timeout
        with patch("threading.Event.wait", return_value=False):
            with self.assertRaises(RuntimeError) as context:
                EnvoyTransportFactory.get_shared_engine(mock_builder)

            self.assertIn(
                "Envoy Mobile engine failed to start within 10 seconds", str(context.exception)
            )

        mock_builder.set_on_engine_running.assert_called_once()
        mock_builder.build.assert_called_once()


if __name__ == "__main__":
    unittest.main()
