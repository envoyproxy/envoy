Added the ``envoy.ai.downstream_api`` and ``envoy.ai.upstream_target`` filter state keys to the
:ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager_protocol_filter_state>`
(alpha). The first describes how the client speaks to the gateway, ahead of the route's
declaration; the second is the complete description of the upstream a request is sent to: its
protocol, authority, endpoint, model and credential name. Endpoints are named presets for
well-known services or templates, and AI filters read both objects. Only trusted,
configuration-driven writers may set the upstream target, and never from request content.
