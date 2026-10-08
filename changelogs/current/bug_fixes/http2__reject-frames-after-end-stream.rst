Fixed abnormal process termination when an HTTP/2 peer sends HEADERS or DATA frames on a stream
that it has already ended. Such frames are now rejected with a codec protocol error, as required by
RFC 9113 Section 5.1, instead of being dispatched to a decoder that may already have been
destroyed. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.http2_reject_frames_after_end_stream`` to ``false``.
