Fixed truncated HTTP/2 responses when a stream was reset after the response was complete but its data frames
were still blocked by the peer's flow control window, for example on a CONNECT tunnel whose request side was
still open. The deferred reset was sent after the first send attempt and the remaining data frames were
dropped. The reset now waits until the frame carrying ``END_STREAM`` has been sent, bounded by the stream flush
timeout, if set. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.http2_deferred_reset_waits_for_end_stream`` to false.
