Updated the gRPC ``ext_authz`` client to read header values from ``HeaderValue.raw_value`` when
``raw_value`` is non-empty, falling back to ``HeaderValue.value`` when ``raw_value`` is empty.
This allows gRPC authorization servers to return non-UTF-8 header values while preserving
backwards compatibility with servers that only populate ``value``. This gRPC client behavior can
be reverted by setting the runtime guard
``envoy.reloadable_features.ext_authz_read_raw_header_value`` to ``false``. Separately, in shadow
mode ``ShadowDecision.response_headers`` now populates ``raw_value`` instead of ``value`` when a
denied response header value contains non-UTF-8 bytes. This shadow-mode change is not controlled
by the runtime guard because valid UTF-8 values are unchanged in ``value`` while non-UTF-8
``value`` strings violated proto3 UTF-8 requirements and broke JSON serialization.
