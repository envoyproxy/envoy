The filter manager now buffers body data stopped at a filter in that filter's own buffer instead
of a single buffer shared by the whole stream, so several filters can buffer concurrently at
different positions in the chain without mixing their data. The stream buffer limit applies to
the total held by all filters' buffers in each direction. This only takes effect when
``envoy.reloadable_features.filter_manager_per_filter_observed_end_stream`` is also enabled, and
can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_per_filter_buffer`` to ``false``.
