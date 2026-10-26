When an upstream HTTP stream is reset because its connection to an :ref:`internal listener
<config_internal_listener>` failed or was closed, the router now reports the connection's transport
failure reason, if any, in the response code details, e.g.
``upstream_reset_before_response_started{connection_termination|<transport failure reason>}``, and
in ``%UPSTREAM_TRANSPORT_FAILURE_REASON%``. This reports why the internal listener closed the
connection, see :ref:`close details <config_internal_listener_close_details>`. Connections to other
upstreams are unchanged. This behavior can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.router_reset_with_connection_failure_reason`` to ``false``.
