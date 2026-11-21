Fixed a crash in the ``envoy.filters.http.mcp`` filter (and ``envoy.filters.http.mcp_router`` behind it) when
a request body that was not yet a complete MCP message, such as JSON that is not JSON-RPC or a truncated body,
was followed by a smaller data frame. The filter now parses each data chunk as it arrives.
