An explicitly configured route :ref:`timeout <envoy_v3_api_field_config.route.v3.RouteAction.timeout>`
is now honored for non-gRPC requests when the route also sets
:ref:`max_stream_duration <envoy_v3_api_field_config.route.v3.RouteAction.max_stream_duration>`.
Previously it was silently ignored. Routes with ``max_stream_duration`` and no explicit ``timeout``
are unaffected. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.route_timeout_with_max_stream_duration`` to ``false``.
