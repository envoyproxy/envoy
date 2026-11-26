Fixed the ``mcp_json_rest_bridge`` HTTP filter to reject unsupported JSON-RPC notifications, i.e. messages without an
``id`` whose method starts with ``notifications/``, as unsupported methods with an HTTP error status, as required by the
Streamable HTTP transport. Previously, they got HTTP ``200 OK`` with a JSON-RPC ``-32600`` error for the missing ``id``.
Rejected legacy notifications now get HTTP ``400 Bad Request``, and MCP ``2026-07-28`` notifications keep the existing
HTTP ``404 Not Found`` for unsupported methods. A ``notifications/initialized`` message with an ``id`` is now validated
as a request instead of getting ``202 Accepted``.
