Updated the gRPC ``ext_authz`` client to read header values from ``HeaderValue.raw_value`` when
``raw_value`` is non-empty, falling back to ``HeaderValue.value`` when ``raw_value`` is empty.
This allows gRPC authorization servers to return non-UTF-8 header values while preserving
backwards compatibility with servers that only populate ``value``. In shadow mode,
``ShadowDecision.response_headers`` also populates ``raw_value`` instead of ``value`` for non-UTF-8
denied response header values. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.ext_authz_read_raw_header_value`` to ``false``.
