"""Integration tests for the Envoy Mobile Python bindings."""

import threading
import unittest

from library.python.envoy_engine import (
    EngineBuilder,
    EnvoyError,
    ErrorCode,
    FinalStreamIntel,
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
        self.assertTrue(engine_running.wait(timeout=30),
                        "Engine did not start within timeout")
        result = engine.terminate()
        self.assertEqual(result, 0)  # ENVOY_SUCCESS

    def test_engine_builder_chaining(self):
        """EngineBuilder methods return self for chaining."""
        engine_running = threading.Event()
        builder = EngineBuilder()
        result = (
            builder
            .set_log_level(LogLevel.warn)
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

class TestFetchRequest(unittest.TestCase):
    """Tests for making HTTP requests through Envoy Mobile."""

    def _build_engine(self):
        """Helper to build and start an engine."""
        engine_running = threading.Event()
        engine = (
            EngineBuilder()
            .set_log_level(LogLevel.info)
            .add_runtime_guard("dns_cache_set_ip_version_to_remove", True)
            .set_on_engine_running(lambda: engine_running.set())
            .build()
        )
        self.assertTrue(engine_running.wait(timeout=30),
                        "Engine did not start within timeout")
        return engine

    def test_simple_get_request(self):
        """Send a simple GET request and verify we receive a response."""
        engine = self._build_engine()

        request_finished = threading.Event()
        response_status = {}
        response_body_parts = []

        def on_headers(headers, end_stream, stream_intel):
            # Look for :status header.
            if ":status" in headers:
                response_status["code"] = headers[":status"][0]

        def on_data(data, length, end_stream, stream_intel):
            response_body_parts.append(data[:length])

        def on_complete(stream_intel, final_stream_intel):
            request_finished.set()

        def on_error(error, stream_intel, final_stream_intel):
            response_status["error"] = error.message
            request_finished.set()

        stream = (
            engine.stream_client()
            .new_stream_prototype()
            .start(
                on_headers=on_headers,
                on_data=on_data,
                on_complete=on_complete,
                on_error=on_error,
            )
        )

        headers = {
            ":method": "GET",
            ":scheme": "https",
            ":authority": "www.google.com",
            ":path": "/",
        }
        stream.send_headers(headers, end_stream=True)

        self.assertTrue(request_finished.wait(timeout=30),
                        "Request did not complete within timeout")

        # We should either get a successful response or an error
        # (depending on network availability in the test environment).
        if "error" not in response_status:
            self.assertEqual(response_status.get("code"), "200")
            body = b"".join(response_body_parts)
            self.assertGreater(len(body), 0)

        engine.terminate()

    def test_stream_cancel(self):
        """Cancelling a stream fires the on_cancel callback."""
        engine = self._build_engine()

        cancel_event = threading.Event()

        def on_cancel(stream_intel, final_stream_intel):
            cancel_event.set()

        stream = (
            engine.stream_client()
            .new_stream_prototype()
            .start(on_cancel=on_cancel)
        )

        headers = {
            ":method": "GET",
            ":scheme": "https",
            ":authority": "www.google.com",
            ":path": "/",
        }
        stream.send_headers(headers, end_stream=False)
        stream.cancel()

        self.assertTrue(cancel_event.wait(timeout=10),
                        "Cancel callback was not invoked within timeout")

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

    def test_final_stream_intel_fields(self):
        """FinalStreamIntel fields are accessible."""
        intel = FinalStreamIntel()
        intel.stream_start_ms = 1000
        intel.stream_end_ms = 2000
        intel.upstream_protocol = 2
        self.assertEqual(intel.stream_start_ms, 1000)
        self.assertEqual(intel.stream_end_ms, 2000)
        self.assertEqual(intel.upstream_protocol, 2)

    def test_envoy_error_fields(self):
        """EnvoyError fields are accessible."""
        error = EnvoyError()
        error.error_code = ErrorCode.ConnectionFailure
        error.message = "test error"
        error.attempt_count = 3
        self.assertEqual(error.error_code, ErrorCode.ConnectionFailure)
        self.assertEqual(error.message, "test error")
        self.assertEqual(error.attempt_count, 3)

    def test_envoy_error_attempt_count_none(self):
        """EnvoyError attempt_count can be None."""
        error = EnvoyError()
        error.attempt_count = None
        self.assertIsNone(error.attempt_count)

    def test_log_level_values(self):
        """LogLevel enum values are present."""
        self.assertIsNotNone(LogLevel.trace)
        self.assertIsNotNone(LogLevel.debug)
        self.assertIsNotNone(LogLevel.info)
        self.assertIsNotNone(LogLevel.warn)
        self.assertIsNotNone(LogLevel.error)
        self.assertIsNotNone(LogLevel.critical)
        self.assertIsNotNone(LogLevel.off)

    def test_error_code_values(self):
        """ErrorCode enum values are present."""
        self.assertIsNotNone(ErrorCode.UndefinedError)
        self.assertIsNotNone(ErrorCode.StreamReset)
        self.assertIsNotNone(ErrorCode.ConnectionFailure)
        self.assertIsNotNone(ErrorCode.BufferLimitExceeded)
        self.assertIsNotNone(ErrorCode.RequestTimeout)


if __name__ == "__main__":
    unittest.main()
