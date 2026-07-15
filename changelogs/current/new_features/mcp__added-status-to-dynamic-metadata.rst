Added status information to dynamic metadata and filter state for the MCP filter. This provides
visibility into request processing results, such as whether a request was rejected due to
body size limits, parsing errors, or duplicate keys. The status message is kept in lower case,
as is prefixed by the ``mcp_``, to indicate the filter name. Added checks in integration tests
to verify the new status (and other mcp related fields) are logged correctly.
