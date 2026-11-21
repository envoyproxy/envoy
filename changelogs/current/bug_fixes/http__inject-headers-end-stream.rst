Fixed ``injectDecodedHeadersToFilterChain()`` and ``injectEncodedHeadersToFilterChain()`` for a
filter that adds a body to a header-only request or response. A later filter that stopped the
injected headers, and continued them before the body was injected, passed them on as the end of
the stream. This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.http_inject_headers_without_end_stream`` to ``false``.
