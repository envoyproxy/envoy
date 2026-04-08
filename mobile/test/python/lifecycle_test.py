"""Integration tests for the Envoy Mobile Python bindings."""

import threading
import unittest

from envoy_mobile import (
    EngineBuilder,
    LogLevel,
    StreamIntel,
)


class TestEngineLifecycle(unittest.TestCase):
    """Tests for building, running, and terminating an Envoy engine."""

    def test_engine_build_and_terminate(self):
        """Engine can be built, started, and terminated."""
        engine_running = threading.Event()
        engine = (
            EngineBuilder()
            .set_log_level(LogLevel.info)
            .set_on_engine_running(lambda: engine_running.set())
            .build()
        )
        self.assertTrue(engine_running.wait(timeout=30), "Engine did not start within timeout")
        result = engine.terminate()
        self.assertEqual(result, 0)  # ENVOY_SUCCESS

    def test_engine_builder_chaining(self):
        """EngineBuilder methods return self for chaining."""
        engine_running = threading.Event()
        builder = EngineBuilder()
        result = (
            builder.set_log_level(LogLevel.warn)
            .add_connect_timeout_seconds(30)
            .add_dns_refresh_seconds(120)
            .add_dns_failure_refresh_seconds(2, 10)
            .add_dns_query_timeout_seconds(60)
            .add_dns_min_refresh_seconds(30)
            .add_max_connections_per_host(5)
            .set_app_version("1.0.0")
            .set_app_id("test_app")
            .set_device_os("python")
            .enable_http3(False)
            .enable_gzip_decompression(True)
            .enable_brotli_decompression(False)
            .enable_socket_tagging(False)
            .enable_interface_binding(False)
            .enable_drain_post_dns_refresh(False)
            .enforce_trust_chain_verification(True)
            .enable_dns_cache(False)
            .set_on_engine_running(lambda: engine_running.set())
        )
        # All chained calls should return the builder.
        self.assertIsInstance(result, type(builder))
        engine = builder.build()
        self.assertTrue(engine_running.wait(timeout=30))
        engine.terminate()

    def test_dump_stats(self):
        """Engine can dump stats."""
        engine_running = threading.Event()
        engine = (
            EngineBuilder()
            .set_log_level(LogLevel.info)
            .enable_stats_collection(True)
            .set_on_engine_running(lambda: engine_running.set())
            .build()
        )
        self.assertTrue(engine_running.wait(timeout=30))
        stats = engine.dump_stats()
        self.assertIsInstance(stats, str)
        engine.terminate()


class TestPythonTypes(unittest.TestCase):
    """Tests for Python type wrappers."""

    def test_stream_intel_fields(self):
        """StreamIntel fields are accessible."""
        intel = StreamIntel()
        intel.stream_id = 42
        intel.connection_id = 7
        intel.attempt_count = 1
        self.assertEqual(intel.stream_id, 42)
        self.assertEqual(intel.connection_id, 7)
        self.assertEqual(intel.attempt_count, 1)


if __name__ == "__main__":
    unittest.main()
