Fixed :ref:`consecutive_local_origin_failure <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_local_origin_failure>`
outlier detection never ejecting a host in split mode when upstream requests time out. Obtaining an upstream stream no longer
resets the consecutive local origin failure counter. A response from the upstream now resets it instead.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.outlier_keep_local_origin_failures_on_stream_ready`` to ``false``.
