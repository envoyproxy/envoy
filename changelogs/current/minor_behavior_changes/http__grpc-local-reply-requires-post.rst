Requests that carry a gRPC content type (``application/grpc`` or ``application/grpc+...``) but use a
method other than ``POST`` now receive ordinary HTTP local replies (for example ``503`` with the error
text in the body, or headers only for ``HEAD``) instead of a trailers-only ``200`` with
``grpc-status`` and, when present, ``grpc-message`` headers. The gRPC over HTTP/2 protocol requires
``POST``, and a ``200`` response to a ``GET`` is heuristically cacheable by HTTP caches. For the same
reason the :ref:`gRPC HTTP/1.1 reverse bridge <config_http_filters_grpc_http1_reverse_bridge>` no
longer bridges such requests. This behavior can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.grpc_local_reply_requires_post`` to ``false``; the value is read when a
connection manager is created, so a change applies to new connections.
