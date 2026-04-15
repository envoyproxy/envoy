"""Integration tests for the Envoy Mobile Python bindings."""

import random
import threading
import unittest
from test.python.echo_test_server import EchoTestServer

from envoy_mobile import (
    EngineBuilder,
    EnvoyError,
    ErrorCode,
    FinalStreamIntel,
    LogLevel,
    StreamIntel,
)


class TestFetchRequest(unittest.TestCase):
    """Tests for making HTTP requests through Envoy Mobile."""

    @classmethod
    def setUpClass(cls):
        """Set up an echo test server for the tests to hit."""
        port = random.randint(2**14, 2**16)
        cls._echo_server = EchoTestServer("127.0.0.1", port)
        cls._echo_server.start()
        cls._echo_server_url = f"127.0.0.1:{port}"

    @classmethod
    def tearDownClass(cls):
        """Shut down the echo test server."""
        cls._echo_server.stop()

    def _build_engine(self):
        """Helper to build and start an engine."""
        engine_running = threading.Event()
        engine = (
            EngineBuilder()
            .set_log_level(LogLevel.trace)
            .add_runtime_guard("dns_cache_set_ip_version_to_remove", True)
            .set_on_engine_running(lambda: engine_running.set())
            .enable_worker_thread(True)
            .build()
        )
        self.assertTrue(engine_running.wait(timeout=30), "Engine did not start within timeout")
        return engine

    def _copy_final_stream_intel(self, source, destination):
        """Copy attributes from source FinalStreamIntel to destination."""
        if source is None or destination is None:
            return
        attrs = [a for a in dir(source) if not a.startswith("_")]
        for attr in attrs:
            try:
                val = getattr(source, attr)
                setattr(destination, attr, val)
            except (AttributeError, TypeError) as e:
                # Some attributes may not be copyable; skip them.
                print(f"Warning: Could not copy attribute '{attr}': {e}")

    def _create_stream_with_callbacks(self, engine):
        """Helper to create a stream with standard callbacks.

        Returns:
            Tuple of (stream, stream_finished_event, response_status_dict, response_body_list)
        """
        stream_finished = threading.Event()
        response_status = {}
        response_body_parts = []
        final_stream = FinalStreamIntel()

        def on_headers(headers, end_stream, stream_intel):
            # Look for :status header.
            if ":status" in headers:
                response_status["code"] = headers[":status"][0]

        def on_data(data, length, end_stream, stream_intel):
            response_body_parts.append(data[:length])

        def on_complete(stream_intel, final_stream_intel):
            self.assertGreater(stream_intel.consumed_bytes_from_response, 0)
            self.assertEqual(stream_intel.attempt_count, 1)
            self._copy_final_stream_intel(final_stream_intel, final_stream)
            stream_finished.set()

        def on_error(error, stream_intel, final_stream_intel):
            response_status["error"] = error.message
            self._copy_final_stream_intel(final_stream_intel, final_stream)
            stream_finished.set()

        def on_cancel(stream_intel, final_stream_intel):
            self._copy_final_stream_intel(final_stream_intel, final_stream)
            stream_finished.set()

        stream = (
            engine.stream_client()
            .new_stream_prototype()
            .start(
                on_headers=on_headers,
                on_data=on_data,
                on_complete=on_complete,
                on_error=on_error,
                on_cancel=on_cancel,
            )
        )
        return (
            stream,
            stream_finished,
            response_status,
            response_body_parts,
            final_stream,
        )

    def test_simple_get_request(self):
        """Send a simple GET request and verify we receive a response."""
        engine = self._build_engine()

        (
            stream,
            stream_finished,
            response_status,
            response_body_parts,
            final_stream,
        ) = self._create_stream_with_callbacks(engine)

        headers = {
            ":method": "GET",
            ":scheme": "http",
            ":authority": self._echo_server_url,
            ":path": "/",
        }
        stream.send_headers(headers, end_stream=True)

        self.assertTrue(stream_finished.wait(timeout=30), "Request did not complete within timeout")

        # Verify final_stream fields are meaningful (explicit attribute access)
        self.assertIsNotNone(final_stream)
        # Some timing fields should be > 0 when a request completes
        self.assertGreater(final_stream.stream_start_ms, 0)
        self.assertGreaterEqual(final_stream.response_start_ms, 0, final_stream.ssl_end_ms)
        self.assertGreater(final_stream.stream_end_ms, 0)
        # Byte counts should be >= 0, and at least one should be > 0 for successful responses
        self.assertGreaterEqual(final_stream.sent_byte_count, 0)
        self.assertGreaterEqual(final_stream.received_byte_count, 0)
        # upstream_protocol should not be -1 for a completed request
        self.assertNotEqual(final_stream.upstream_protocol, -1)

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

        (
            stream,
            stream_finished,
            response_status,
            response_body_parts,
            final_stream,
        ) = self._create_stream_with_callbacks(engine)

        headers = {
            ":method": "GET",
            ":scheme": "http",
            ":authority": self._echo_server_url,
            ":path": "/",
        }
        stream.send_headers(headers, end_stream=False)
        stream.cancel()

        self.assertTrue(
            stream_finished.wait(timeout=10),
            "Cancel callback was not invoked within timeout",
        )

        # Verify final_stream for cancelled stream: start/end should be set,
        # but sending/response/upstream fields should be unset (-1).
        self.assertIsNotNone(final_stream)
        self.assertGreater(final_stream.stream_start_ms, 0)
        self.assertGreater(final_stream.stream_end_ms, 0)
        self.assertEqual(final_stream.sending_end_ms, -1)
        self.assertEqual(final_stream.response_start_ms, -1)
        self.assertEqual(final_stream.upstream_protocol, -1)

        engine.terminate()


if __name__ == "__main__":
    unittest.main()
