Fixed a bug where an upstream HTTP/2 or HTTP/3 ``RST_STREAM(NO_ERROR)`` received after a complete
response would cause the response to be discarded and replaced with an error. This behavior is
common with some gRPC clients and servers, but is often intermittent. This behavior can be
temporarily reverted by setting the runtime flag
``envoy.reloadable_features.http_preserve_rst_no_error`` to ``false``.
