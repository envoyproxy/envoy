adaptive_concurrency: added an optional :ref:`absolute latency buffer
<envoy_v3_api_field_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig.MinimumRTTCalculationParams.min_latency_delta>`
and an opt-in :ref:`EWMA baseline mode
<envoy_v3_api_field_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig.baseline_mode>`
to adaptive concurrency. Both features are disabled by default, preserving existing behavior.
