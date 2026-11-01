Added validation that stateless MCP ``2026-07-28`` requests include
``params._meta["io.modelcontextprotocol/clientCapabilities"]`` as a JSON object. Requests where the
field is missing or is not an object are rejected with a ``400`` ``Invalid params`` (``-32602``)
JSON-RPC error. Individual capability contents are not validated.
