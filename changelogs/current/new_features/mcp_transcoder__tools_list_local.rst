Added local response handling for the ``tools/list`` JSON-RPC method in the MCP JSON REST bridge
filter. Configuring ``tools_list_local`` will cause the filter to directly generate and serve the
available tools list response without sending a request upstream. This may be configured on a
per-route basis.
