Added :ref:`lazy_initialization <envoy_v3_api_field_extensions.filters.http.mcp_router.v3.McpRouter.lazy_initialization>`
option to the MCP router filter. When enabled, the ``initialize`` response is returned immediately
without contacting backends. Each backend is initialized on-demand when a request first routes to it,
avoiding slow or misbehaving backends from blocking client initialization.
