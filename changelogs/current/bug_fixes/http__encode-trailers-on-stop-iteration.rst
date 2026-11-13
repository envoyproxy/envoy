Fixed response trailers added via ``addEncodedTrailers()`` during ``encodeData()`` being dropped when a
later encoder filter stops iteration, which left filters that finish streaming responses on
``encodeTrailers()`` waiting forever for the end of the stream. The added trailers are now forwarded to
the remaining encoder filters, mirroring the request path. This change is guarded by the runtime guard
``envoy.reloadable_features.filter_manager_encode_trailers_on_stop_iteration``.
