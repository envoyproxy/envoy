Fixed the ``mcp_json_rest_bridge`` HTTP filter to handle a request with an ``MCP-Protocol-Version: 2026-07-28``
header as a stateless MCP ``2026-07-28`` request when ``max_supported_protocol_version`` is ``2026-07-28``, even
when ``params._meta["io.modelcontextprotocol/protocolVersion"]`` is missing or is not a string. Such requests are
now rejected with HTTP ``400`` and JSON-RPC error ``-32602`` (``Invalid params``) instead of falling back to legacy
protocol version validation. Mismatched and unsupported protocol versions are still rejected with HTTP ``400`` and
``-32020`` or ``-32022``. Requests without a ``2026-07-28`` header or string protocol version metadata keep their
legacy behavior.
