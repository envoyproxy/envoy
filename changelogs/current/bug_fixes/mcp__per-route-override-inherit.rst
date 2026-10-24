Fixed the MCP filter's per-route
:ref:`McpOverride <envoy_v3_api_msg_extensions.filters.http.mcp.v3.McpOverride>` resetting listener
settings that it does not set. An override which only sets ``traffic_mode`` now keeps the
listener's ``clear_route_cache``, ``parser_config``, ``request_storage_mode`` and
``reject_duplicate_keys``, as before ``1.40``. ``clear_route_cache`` and ``reject_duplicate_keys``
are now ``google.protobuf.BoolValue`` and apply only when set, ``parser_config`` applies only when
set, and ``request_storage_mode`` applies only when not ``MODE_UNSPECIFIED``.
