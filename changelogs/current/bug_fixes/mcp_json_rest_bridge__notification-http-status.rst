Fixed the ``mcp_json_rest_bridge`` HTTP filter to handle JSON-RPC notifications, i.e. messages without an ``id`` whose
method starts with ``notifications/``, as required by the Streamable HTTP transport. Previously, notifications other than
``notifications/initialized`` were rejected with HTTP ``200 OK`` and a JSON-RPC ``-32600`` error for the missing ``id``.
A legacy ``notifications/cancelled`` notification is now accepted with HTTP ``202 Accepted`` and an empty body, and
ignored, since the bridge cannot cancel a request in progress. Other legacy notifications, and legacy notifications with
an unsupported protocol version, are rejected with HTTP ``400 Bad Request``. MCP ``2026-07-28`` notifications, none of
which are supported, are rejected with HTTP ``404 Not Found`` and a JSON-RPC ``-32601`` error, also without the request
metadata headers and ``params._meta`` fields that requests require. Messages with an ``id``, even a ``null`` one, are
validated as requests, so a ``notifications/initialized`` message with an ``id`` is now rejected as an unsupported
method, with its ``id`` in the JSON-RPC error response, instead of getting ``202 Accepted``.
