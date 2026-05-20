Added support for configuring a
:ref:`budget_interval <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.RetryBudget.budget_interval>`
on the retry budget circuit breaker, allowing new requests to be considered for the duration of the
interval when calculating the retry budget. Defaults to 0ms (preserving existing behavior).
