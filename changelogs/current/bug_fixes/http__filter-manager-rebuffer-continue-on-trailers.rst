Fixed a bug in filter manager trailer forwarding where a filter resuming from
``StopAllIterationAndBuffer`` or ``StopAllIterationAndWatermark`` that re-buffers the body in
``decodeData()`` / ``encodeData()`` before returning ``Continue`` from ``decodeTrailers()`` /
``encodeTrailers()`` dropped the buffered body on the nested continuation. Guarded by
``envoy.reloadable_features.filter_manager_forward_held_trailers``.
