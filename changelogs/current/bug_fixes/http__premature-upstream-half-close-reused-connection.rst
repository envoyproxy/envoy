Fixed a crash when ``envoy.reloadable_features.allow_multiplexed_upstream_half_close`` is enabled and
an HTTP/1 upstream completes its response before the request is complete, on an upstream connection
that an earlier request completed on. The HTTP/1 codec did not reset the stream, because its
encode-complete state was left over from the earlier request, so the router kept using a request
encoder that the connection pool had freed when the downstream sent more of the request or reset.
