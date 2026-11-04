Fixed the ``mcp_json_rest_bridge`` HTTP filter to respond with HTTP ``404 Not Found`` instead of ``200 OK`` when a
stateless MCP ``2026-07-28`` request uses a method that the bridge does not implement, as required by the Streamable
HTTP transport. The JSON-RPC error code is unchanged (``-32601``). This also applies to the legacy ``initialize`` and
``notifications/initialized`` methods sent as stateless requests. Requests that use MCP ``2025-11-25`` or earlier still
receive ``200 OK``.
