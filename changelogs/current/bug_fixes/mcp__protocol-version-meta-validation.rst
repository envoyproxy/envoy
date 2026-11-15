Fixed handling of MCP ``2026-07-28`` requests whose ``params._meta["io.modelcontextprotocol/protocolVersion"]`` is
missing or is not a string. The :ref:`MCP filter <config_http_filters_mcp>` in ``REJECT_NO_MCP`` mode and the
``mcp_json_rest_bridge`` HTTP filter now reject them with HTTP ``400`` and JSON-RPC error ``-32602``. Previously, the
MCP filter accepted missing metadata and rejected non-string metadata as a ``-32020`` header mismatch counted in
``header_mismatch``, and the bridge handled requests with an ``MCP-Protocol-Version: 2026-07-28`` header but without
valid metadata as legacy requests.
