"""Unit tests for EnvoyTransportFactory."""

import sys
import unittest
from unittest.mock import MagicMock

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

    def test_get_shared_engine_singleton(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()
        mock_builder.build.return_value = mock_engine

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
        mock_builder.build.return_value = mock_engine

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
        mock_builder_instance.build.return_value = mock_engine

        # Call without explicit builder
        engine = EnvoyTransportFactory.get_shared_engine()

        # Verify default builder was used
        mock_envoy_engine.EngineBuilder.assert_called_once()
        self.assertEqual(engine, mock_engine)


if __name__ == "__main__":
    unittest.main()
