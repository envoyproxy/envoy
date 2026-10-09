Added :ref:`early_terminate_when_routable
<envoy_v3_api_field_extensions.filters.http.mcp.v3.Mcp.early_terminate_when_routable>` to the
:ref:`MCP filter <config_http_filters_mcp>`. When enabled, the filter stops parsing and buffering
the request body as soon as all required routing attributes for the request's method have been
collected (for example ``method`` and ``params.name`` for ``tools/call``), instead of buffering up
to ``max_request_body_size`` and waiting for the root JSON object to close. This decouples routing
from body size so a large trailing payload (for example ``params.arguments``) does not need to be
buffered just to route the request. Early termination is skipped when ``reject_duplicate_keys``,
trace context or baggage propagation is configured, or when ``attribute_source`` is not ``BODY``.
Defaults to false.
