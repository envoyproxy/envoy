Fixed a bug where upstream connection pools could be shared between requests that resolved to different
``transport_socket_matcher`` matches via the ``envoy.matching.inputs.transport_socket_filter_state`` input,
when the filter state object driving the match did not implement ``Hashable`` (for example
``envoy.string``). The resolved match is now part of the HTTP and TCP connection pool keys.
