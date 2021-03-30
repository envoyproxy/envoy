import threading
import unittest

from library.python import envoy_engine


class TestSendHeaders(unittest.TestCase):
    def test_send_headers(self):
        engine_running = threading.Event()
        engine = (
            envoy_engine.EngineBuilder()
            .add_log_level(envoy_engine.LogLevel.Error)
            .set_on_engine_running(lambda: engine_running.set())
            .build()
        )
        engine_running.wait()

        response_lock = threading.Lock()
        response = {"status": None, "data": bytearray()}

        def _on_headers(response_headers: envoy_engine.ResponseHeaders, _: bool):
            with response_lock:
                response["status"] = response_headers.http_status()

        def _on_data(response_data: bytes, _: bool):
            with response_lock:
                response["data"].extend(response_data)

        stream_complete = threading.Event()
        stream = (
            engine
            .stream_client()
            .new_stream_prototype()
            .set_on_headers(_on_headers)
            .set_on_data(_on_data)
            # unused:
            # .set_on_metadata(on_metadata)
            # .set_on_trailers(on_trailers)
            .set_on_complete(lambda: stream_complete.set())
            .set_on_error(lambda _: stream_complete.set())
            .set_on_cancel(lambda: stream_complete.set())
            .start()
        )
        stream.send_headers(
            envoy_engine.RequestHeadersBuilder(
                envoy_engine.RequestMethod.GET,
                "https",
                "api.lyft.com",
                "/ping",
            )
            .build(),
            True,
        )
        stream_complete.wait()

        assert response["status"] == 200
        assert bytes(response["data"]) == b"{}"

if __name__ == "__main__":
    unittest.main()
