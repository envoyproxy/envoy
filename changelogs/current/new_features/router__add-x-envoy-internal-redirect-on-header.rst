* router: added per-request :ref:`x-envoy-internal-redirect-on
  <config_http_filters_router_x-envoy-internal-redirect-on>` header that activates internal
  redirect on a per-request basis without requiring a route-level ``internal_redirect_policy``.
  The header value is a comma-separated list of redirect status codes to follow (``301``, ``302``,
  ``303``, ``307``, ``308``) or the shorthand ``3xx`` to enable all of them.
