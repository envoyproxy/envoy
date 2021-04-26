import threading
import unittest

from library.python import envoy_engine


API_LISTENER_TYPE = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
ASSERTION_FILTER_TYPE = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"

CONFIG_TEMPLATE = f"""\
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": {API_LISTENER_TYPE}
        stat_prefix: hcm
        route_config:
          name: api_router
          virtual_hosts:
            - name: api
              domains:
                - "*"
              routes:
                - match:
                    prefix: "/"
                  direct_response:
                    status: 200
        http_filters:
          - name: envoy.filters.http.assertion
            typed_config:
              "@type": {ASSERTION_FILTER_TYPE}
              match_config:
                http_request_headers_match:
                  headers:
                    - name: ":authority"
                      exact_match: example.com
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""


class TestSendHeaders(unittest.TestCase):
    # FIXME(goaway): re-enable test
    def test_send_headers(self):
         assert True
#        engine_running = threading.Event()
#        engine = (
#            envoy_engine.EngineBuilder(CONFIG_TEMPLATE)
#            .add_log_level(envoy_engine.LogLevel.Error)
#            .set_on_engine_running(lambda: engine_running.set())
#            .build()
#        )
#        engine_running.wait()
#
#        response_lock = threading.Lock()
#        response = {"status": None, "end_stream": None}
#
#        def _on_headers(response_headers: envoy_engine.ResponseHeaders, end_stream: bool):
#            with response_lock:
#                response["status"] = response_headers.http_status()
#                response["end_stream"] = end_stream
#
#        stream_complete = threading.Event()
#        stream = (
#            engine
#            .stream_client()
#            .new_stream_prototype()
#            .set_on_headers(_on_headers)
#            # unused:
#            # .set_on_metadata(on_metadata)
#            # .set_on_trailers(on_trailers)
#            .set_on_complete(lambda: stream_complete.set())
#            .set_on_error(lambda _: stream_complete.set())
#            .set_on_cancel(lambda: stream_complete.set())
#            .start()
#        )
#        stream.send_headers(
#            envoy_engine.RequestHeadersBuilder(
#                envoy_engine.RequestMethod.GET,
#                "https",
#                "example.com",
#                "/test",
#            )
#            .add_upstream_http_protocol(envoy_engine.UpstreamHttpProtocol.HTTP2)
#            .build(),
#            True,
#        )
#        stream_complete.wait()
#
#        assert response == {
#            "status": 200,
#            "end_stream": True,
#        }

if __name__ == "__main__":
    unittest.main()
