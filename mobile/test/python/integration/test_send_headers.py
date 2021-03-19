import unittest

from gevent.event import Event

from library.python import envoy_engine
from library.python.gevent_util import GeventEngineBuilder


class TestSendHeaders(unittest.TestCase):
    def test_send_headers(self):
        engine_running = Event()
        engine = (
            GeventEngineBuilder()
            .add_log_level(envoy_engine.LogLevel.Error)
            .set_on_engine_running(lambda: engine_running.set())
            .build()
        )
        engine_running.wait()

        status = None
        data = b""

        def _on_headers(response_headers: envoy_engine.ResponseHeaders, _: bool):
            nonlocal status
            status = response_headers.http_status()

        def _on_data(response_data: bytes, _: bool):
            nonlocal data
            data += response_data

        stream_complete = Event()
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
            .set_on_error(lambda error: stream_complete.set())
            .set_on_cancel(lambda cancel: stream_complete.set())
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

        assert status == 200
        assert data == b"{}"

if __name__ == "__main__":
    unittest.main()
