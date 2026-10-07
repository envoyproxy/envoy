The :ref:`x-envoy-upstream-stream-duration-ms
<config_http_filters_router_x-envoy-upstream-stream-duration-ms>` request header is now removed from
external requests, in line with the other router timeout and retry headers, so it is only honored
for requests from internal clients. Previously any client could use it to set the maximum upstream
stream duration. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.sanitize_upstream_stream_duration_header`` to ``false``.
