Fixed the :ref:`MCP filter <config_http_filters_mcp>` treating JSON-RPC messages that contain both
``method`` and ``result`` (or ``error``) as responses. Such messages are now treated as requests, so
in ``REJECT_NO_MCP`` mode they can no longer skip the required MCP ``2026-07-28`` ``params._meta``
fields, and their ``method`` dynamic metadata is no longer replaced with ``__jsonrpc_response``.
