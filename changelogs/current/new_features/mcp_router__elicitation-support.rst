Added elicitation support to the MCP router filter. The gateway now transparently handles
server-to-client requests (``elicitation/create``, ``sampling/createMessage``, ``roots/list``)
by rewriting JSON-RPC ``id`` fields for correct routing in multiplexing mode and forwarding
client responses back to the originating backend.
