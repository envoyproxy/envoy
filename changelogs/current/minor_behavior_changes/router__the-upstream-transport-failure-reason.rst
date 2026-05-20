The upstream transport failure reason (e.g. TLS certificate validation errors) is no longer included
in the HTTP response body sent to downstream clients. It remains available in access logs via
``%UPSTREAM_TRANSPORT_FAILURE_REASON%``. This behavioral change can be temporarily reverted by setting
runtime guard ``envoy.reloadable_features.hide_transport_failure_reason_in_response_body`` to ``false``.
This is being changed because in many cases the upstream failure details are inappropriate to send to
the downstream client as it discloses too many internal details.
