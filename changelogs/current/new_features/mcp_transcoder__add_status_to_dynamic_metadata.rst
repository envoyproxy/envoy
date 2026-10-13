The MCP JSON REST bridge filter (``envoy.filters.http.mcp_json_rest_bridge``) now exports
its execution status and the backend response code (``backend_response_code``) to dynamic
metadata, including the pass-through status
``mcp_json_rest_bridge_internal_tools_list_passthrough``.
