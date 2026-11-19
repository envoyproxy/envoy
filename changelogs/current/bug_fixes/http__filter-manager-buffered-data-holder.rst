Fixed the filter manager sending on data that an earlier filter was still buffering. A filter that
stopped the headers, and continued while an earlier filter was buffering the body (having returned
``StopIterationAndBuffer`` or ``StopIterationAndWatermark``), sent the data buffered so far to the
later filters, which then received it again when the earlier filter continued. This affected, for
example, an upstream HTTP filter that buffers the request body, when the upstream connection
became ready while the body was arriving. The filter now continues the headers only, and the data
follows when the earlier filter continues. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_buffered_data_holder`` to ``false``.
