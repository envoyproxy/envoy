from library.python.envoy_requests.common.engine import Engine
from library.python.envoy_requests import get


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


def test_send_headers():
    Engine.config_template_override = CONFIG_TEMPLATE
    response = get("https://example.com/test")
    assert response.status_code == 200
