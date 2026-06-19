mcp_json_rest_bridge: Fixed a bug where headers-only upstream responses (e.g., HTTP 204 No Content)
were passed through to MCP clients without a JSON-RPC response body, causing MCP SDK timeouts or
exceptions. The filter now synthesizes a valid JSON-RPC response: an empty ``ToolResult`` for
``tools/call`` requests and a server error for ``tools/list`` requests.
