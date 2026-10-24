When a route timeout fires, the router now records where the newest upstream request was stuck
in the :ref:`response code details <config_http_conn_man_details>`. A request that never received a
connection from the pool (still queued, connecting, or in the TLS handshake) is reported as
``response_timeout{connecting}``, and a request whose body had not been fully sent upstream is
reported as ``response_timeout{sending_request}``. A request that was fully sent and was waiting for
response headers keeps the existing ``response_timeout`` value. This behavioral change can be
temporarily reverted by setting runtime guard
``envoy.reloadable_features.response_timeout_phase_details`` to ``false``.
