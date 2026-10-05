Added validation to the :ref:`MCP filter <config_http_filters_mcp>` that MCP ``2026-07-28`` requests
include ``io.modelcontextprotocol/clientCapabilities`` in ``params._meta`` when ``traffic_mode`` is
``REJECT_NO_MCP``. Requests missing the field are rejected with HTTP 400 and JSON-RPC error
``-32602``. Only JSON-RPC requests are checked; notifications are exempt. In this mode,
``attribute_source: HEADERS`` now parses the request body for ``2026-07-28`` requests.
