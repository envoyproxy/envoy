Reserved flag bits on downstream HTTP/2 ``CONTINUATION`` frames are now ignored, per RFC 9113
§6.10, so they can no longer alias ``END_STREAM`` on the accumulated ``HEADERS`` frame and
desynchronize Envoy's stream state from the underlying codec. This change can be temporarily
reverted by setting the runtime guard ``envoy.reloadable_features.http2_mask_continuation_flags``
to ``false``.

