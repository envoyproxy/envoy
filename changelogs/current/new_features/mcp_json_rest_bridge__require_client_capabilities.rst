Added validation that stateless MCP ``2026-07-28`` requests include
``params._meta["io.modelcontextprotocol/clientCapabilities"]``. Requests missing the field are
rejected with a ``400`` ``Invalid params`` (``-32602``) JSON-RPC error. Only the presence of the
field is checked.
