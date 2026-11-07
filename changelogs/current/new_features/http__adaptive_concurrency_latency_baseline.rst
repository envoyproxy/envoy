adaptive_concurrency: added an optional :ref:`absolute latency buffer
<envoy_v3_api_field_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig.MinimumRTTCalculationParams.min_latency_delta>`
and a separate :ref:`EWMA controller
<envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.EwmaControllerConfig>`
that uses a continuously learned latency baseline without minRTT probes. Existing configurations are unchanged.
