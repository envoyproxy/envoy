"""Python equivalent of the C++ fetch_client example.

Usage:
    bazel run //examples/python/fetch_client -- https://www.google.com
"""

import sys
import threading

from library.python.envoy_engine import (
    EngineBuilder,
    LogLevel,
)


class FetchHandler:
    """Handles a single HTTP request with callbacks as methods."""

    def __init__(self, engine, url_string: str):
        self.engine = engine
        self.url_string = url_string
        self.request_finished = threading.Event()
        self.success = True
        self.final_stream_intel = None

    def _parse_url(self):
        """Parse URL into components."""
        if "://" in self.url_string:
            scheme, rest = self.url_string.split("://", 1)
        else:
            scheme = "https"
            rest = self.url_string

        if "/" in rest:
            authority, path = rest.split("/", 1)
            path = "/" + path
        else:
            authority = rest
            path = "/"

        return scheme, authority, path

    def on_headers(self, headers, end_stream, stream_intel):
        print(
            f"Received headers on connection: {stream_intel.connection_id}",
            file=sys.stderr,
        )
        for key, values in headers.items():
            for val in values:
                print(f"  {key}: {val}", file=sys.stderr)

    def on_data(self, data, length, end_stream, stream_intel):
        sys.stdout.buffer.write(data[:length])
        if end_stream:
            print("\nReceived final data", file=sys.stderr)

    def on_complete(self, stream_intel, final_stream_intel):
        self.final_stream_intel = final_stream_intel
        duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
        print(f"Request finished after {duration}ms", file=sys.stderr)
        assert stream_intel.consumed_bytes_from_response > 0, "Expected to receive response data"
        self.request_finished.set()

    def on_error(self, error, stream_intel, final_stream_intel):
        self.final_stream_intel = final_stream_intel
        self.success = False
        duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
        print(
            f"Request failed after {duration}ms with error: {error.message}",
            file=sys.stderr,
        )
        self.request_finished.set()

    def on_cancel(self, stream_intel, final_stream_intel):
        self.final_stream_intel = final_stream_intel
        self.success = False
        duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
        print(f"Request cancelled after {duration}ms", file=sys.stderr)
        self.request_finished.set()

    def fetch(self):
        """Execute the HTTP request and wait for completion."""
        scheme, authority, path = self._parse_url()
        print(f"Fetching url: {scheme}://{authority}{path}", file=sys.stderr)

        # Create stream with callbacks.
        stream = (
            self.engine.stream_client()
            .new_stream_prototype()
            .start(
                on_headers=self.on_headers,
                on_data=self.on_data,
                on_complete=self.on_complete,
                on_error=self.on_error,
                on_cancel=self.on_cancel,
            )
        )

        # Send request headers.
        headers = {
            ":method": "GET",
            ":scheme": scheme,
            ":authority": authority,
            ":path": path,
        }
        stream.send_headers(headers, end_stream=True)

        # Wait for completion.
        self.request_finished.wait()

        return self.success


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <url> [<url> ...]", file=sys.stderr)
        sys.exit(1)

    urls = sys.argv[1:]

    # Build engine.
    engine_running = threading.Event()
    engine = (
        EngineBuilder()
        .set_log_level(LogLevel.trace)
        .add_runtime_guard("dns_cache_set_ip_version_to_remove", True)
        .add_runtime_guard("quic_no_tcp_delay", True)
        .set_on_engine_running(lambda: engine_running.set())
        .build()
    )
    print("Waiting for engine to start...", file=sys.stderr)
    engine_running.wait()
    print("Engine started.", file=sys.stderr)

    # Fetch each URL
    exit_code = 0
    for url_string in urls:
        handler = FetchHandler(engine, url_string)
        if not handler.fetch():
            exit_code = 1

    engine.terminate()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
