Removed the 10MB (10485760 bytes) upper bound on the MCP filter's
:ref:`max_request_body_size <envoy_v3_api_field_extensions.filters.http.mcp.v3.Mcp.max_request_body_size>`
and the per-route
:ref:`max_request_body_size <envoy_v3_api_field_extensions.filters.http.mcp.v3.McpOverride.max_request_body_size>`.
Any ``uint32`` value is now accepted; the 8KB default is unchanged.
