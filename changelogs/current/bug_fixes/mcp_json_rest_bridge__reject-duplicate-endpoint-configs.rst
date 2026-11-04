Fixed the ``mcp_json_rest_bridge`` HTTP filter per-route configuration validation to reject duplicate ``(host, path)``
endpoint configurations instead of silently merging tools and ignoring subsequent ``tool_list_http_rule`` or
``tool_list_local`` settings.
