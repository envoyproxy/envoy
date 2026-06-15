"""Unit tests for EnvoyTransportFactory."""

import sys
import threading
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

        # Track all threading.Event instances to assert they are always waited for
        self.created_events = []
        self.simulate_timeout = False
        self.original_event = threading.Event

        def mocked_event():
            # Wrap a real event in a MagicMock to track wait calls
            ev = MagicMock(wraps=self.original_event())
            if self.simulate_timeout:
                ev.wait.return_value = False
            self.created_events.append(ev)
            return ev

        self.event_patcher = patch("threading.Event", side_effect=mocked_event)
        self.event_patcher.start()

    def tearDown(self):
        self.event_patcher.stop()
        # Always assert that every synchronization event created was indeed waited for!
        for ev in self.created_events:
            self.assertTrue(
                ev.wait.called,
                "The engine running synchronization event was not waited for",
            )

    def _setup_mock_builder(self, mock_builder, mock_engine, auto_fire_callback=True):
        """Helper to configure a mock builder."""
        running_callback = None

        def set_on_engine_running(callback):
            nonlocal running_callback
            running_callback = callback
            return mock_builder

        def build():
            # Always assert that running_callback is assigned before build() is called
            self.assertIsNotNone(
                running_callback,
                "builder.set_on_engine_running must be called before builder.build()",
            )
            if auto_fire_callback:
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

    def test_get_shared_engine_waits_for_running(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()

        # Do NOT auto-fire callback during build() so we can manually control the timing
        self._setup_mock_builder(mock_builder, mock_engine, auto_fire_callback=False)

        # Capture the callback reference during the test
        engine_running_callback = None
        original_side_effect = mock_builder.set_on_engine_running.side_effect

        def set_on_engine_running_capture(callback):
            nonlocal engine_running_callback
            engine_running_callback = callback
            return original_side_effect(callback)

        mock_builder.set_on_engine_running.side_effect = set_on_engine_running_capture

        # Run get_shared_engine in a separate thread because it blocks waiting for the callback
        result = []
        exception = []

        def target():
            try:
                eng = EnvoyTransportFactory.get_shared_engine(mock_builder)
                result.append(eng)
            except Exception as e:
                exception.append(e)

        t = threading.Thread(target=target)
        t.start()

        # Wait a bit, verify it is still waiting (result is empty)
        t.join(timeout=0.1)
        self.assertTrue(t.is_alive())
        self.assertEqual(len(result), 0)

        # Now manually trigger the callback
        self.assertIsNotNone(engine_running_callback)
        engine_running_callback()

        # The thread should now finish successfully
        t.join(timeout=1.0)
        self.assertFalse(t.is_alive())
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0], mock_engine)
        self.assertEqual(len(exception), 0)

        mock_builder.set_on_engine_running.assert_called_once()
        mock_builder.build.assert_called_once()

    def test_get_shared_engine_timeout(self):
        # Setup mocks
        mock_engine = MagicMock()
        mock_builder = MagicMock()
        # Disable auto-fire of callback
        self._setup_mock_builder(mock_builder, mock_engine, auto_fire_callback=False)

        # Tell our event patcher to simulate a timeout
        self.simulate_timeout = True

        with self.assertRaises(RuntimeError) as context:
            EnvoyTransportFactory.get_shared_engine(mock_builder)

        self.assertIn(
            "Envoy Mobile engine failed to start within 10 seconds",
            str(context.exception),
        )

        mock_builder.set_on_engine_running.assert_called_once()
        mock_builder.build.assert_called_once()


if __name__ == "__main__":
    unittest.main()
