Fixed oghttp2 (``envoy.reloadable_features.http2_use_oghttp2``) dropping trailers when the body exactly exhausted
the peer's flow-control window before the trailers were encoded, for example a 65535-byte gRPC response to a
client using the default initial window. The stream was ended by an empty DATA frame and the client never received
the trailers. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.http2_trailers_after_exhausted_window`` to false.
