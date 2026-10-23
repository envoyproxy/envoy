Fixed the :ref:`MCP filter <config_http_filters_mcp>` to reject MCP ``2026-07-28`` requests whose
``params._meta["io.modelcontextprotocol/protocolVersion"]`` is missing or is not a string with HTTP
``400`` and JSON-RPC error ``-32602`` (``Invalid params``) when ``traffic_mode`` is ``REJECT_NO_MCP``.
Previously, missing metadata was accepted and a non-string value was rejected as a ``-32020`` header
mismatch. A string value that does not match the ``MCP-Protocol-Version`` header is still rejected
with ``-32020``. Missing or non-string metadata no longer increments the ``header_mismatch`` counter.
As with ``io.modelcontextprotocol/clientCapabilities``, only JSON-RPC requests must include the
field. Such requests are still forwarded in ``PASS_THROUGH`` mode, and legacy requests are unchanged.
