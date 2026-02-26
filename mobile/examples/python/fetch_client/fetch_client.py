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

    for url_string in urls:
        # Parse URL components.
        # Simple URL parsing for scheme, host, port, path.
        if "://" in url_string:
            scheme, rest = url_string.split("://", 1)
        else:
            scheme = "https"
            rest = url_string

        if "/" in rest:
            authority, path = rest.split("/", 1)
            path = "/" + path
        else:
            authority = rest
            path = "/"

        print(f"Fetching url: {scheme}://{authority}{path}", file=sys.stderr)

        request_finished = threading.Event()
        status = {"value": 0}  # 0 = success

        def on_headers(headers, end_stream, stream_intel):
            print(f"Received headers on connection: {stream_intel.connection_id}",
                  file=sys.stderr)
            for key, values in headers.items():
                for val in values:
                    print(f"  {key}: {val}", file=sys.stderr)

        def on_data(data, length, end_stream, stream_intel):
            sys.stdout.buffer.write(data[:length])
            if end_stream:
                print("\nReceived final data", file=sys.stderr)

        def on_complete(stream_intel, final_stream_intel):
            duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
            print(f"Request finished after {duration}ms", file=sys.stderr)
            request_finished.set()

        def on_error(error, stream_intel, final_stream_intel):
            status["value"] = 1
            duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
            print(f"Request failed after {duration}ms with error: {error.message}",
                  file=sys.stderr)
            request_finished.set()

        def on_cancel(stream_intel, final_stream_intel):
            duration = final_stream_intel.stream_end_ms - final_stream_intel.stream_start_ms
            print(f"Request cancelled after {duration}ms", file=sys.stderr)
            request_finished.set()

        # Create stream with callbacks.
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

        # Send request headers.
        headers = {
            ":method": "GET",
            ":scheme": scheme,
            ":authority": authority,
            ":path": path,
        }
        stream.send_headers(headers, end_stream=True)

        # Wait for completion.
        request_finished.wait()

    engine.terminate()
    sys.exit(status["value"])


if __name__ == "__main__":
    main()
