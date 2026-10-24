Removed runtime flag ``envoy.reloadable_features.allow_multiplexed_upstream_half_close`` and its code
paths. HTTP/2 and HTTP/3 upstreams can no longer half close the stream before the downstream: the
stream is reset when the response completes before the request, as it is for HTTP/1 upstreams.
Independent half close is kept for TCP upstreams, see
``envoy.reloadable_features.tcp_tunnel_allow_upstream_half_close``.
