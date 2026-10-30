Deprecated the MCP filter's per-route
:ref:`clear_route_cache <envoy_v3_api_field_extensions.filters.http.mcp.v3.McpOverride.clear_route_cache>`
and
:ref:`reject_duplicate_keys <envoy_v3_api_field_extensions.filters.http.mcp.v3.McpOverride.reject_duplicate_keys>`
in favor of
:ref:`clear_route_cache_override <envoy_v3_api_field_extensions.filters.http.mcp.v3.McpOverride.clear_route_cache_override>`
and
:ref:`reject_duplicate_keys_override <envoy_v3_api_field_extensions.filters.http.mcp.v3.McpOverride.reject_duplicate_keys_override>`.
The new fields inherit the listener setting when unset and can turn the setting off for a route.
The deprecated fields now only apply when ``true``.
