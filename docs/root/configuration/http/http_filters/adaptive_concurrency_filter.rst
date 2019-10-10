.. _config_http_filters_adaptive_concurrency:

Adaptive Concurrency
====================

.. attention::

  The adaptive concurrency filter is experimental and is currently under active development.

Overview
--------


Configuration
-------------

Concurrency Controllers
-----------------------

Gradient Controller
^^^^^^^^^^^^^^^^^^^


Example Configuration
---------------------
An example filter configuration:

.. code-block:: yaml
  name: envoy.filters.http.adaptive_concurrency
  config:
    gradient_controller_config:
      sample_aggregate_percentile:
        value: 50
      concurrency_limit_params:
        concurrency_update_interval: 0.1s
      min_rtt_calc_params:
        jitter:
          value: 15
        interval: 60s
        request_count: 50
    enabled:
      default_value: true
      runtime_key: "adaptive_concurrency.enabled"

Runtime
-------

The adaptive concurrency filter supports the following runtime settings:

adaptive_concurrency.enabled
    Overrides whether the adaptive concurrency filter will use the concurrency controller for
    forwarding decisions. If set to `false`, the filter will be a no-op. Defaults to what is
    specified for `enabled` in the filter configuration.

adaptive_concurrency.gradient_controller.min_rtt_calc_interval_ms
    Overrides the interval in which the ideal round-trip time (minRTT) will be recalculated.

adaptive_concurrency.gradient_controller.min_rtt_aggregate_request_count
    Overrides the number of requests sampled for calculation of the minRTT.

adaptive_concurrency.gradient_controller.jitter
    Overrides the random delay introduced to the minRTT calculation start time. A value of `10`
    indicates a random delay of 10% of the configured interval. The runtime value specified is
    clamped to the range [0,100].

adaptive_concurrency.gradient_controller.sample_rtt_calc_interval_ms
    Overrides the interval in which the concurrency limit is recalculated based on sampled latencies.

adaptive_concurrency.gradient_controller.max_concurrency_limit
    Overrides the maximum allowed concurrency limit.

adaptive_concurrency.gradient_controller.max_gradient
    Overrides the maximum allowed gradient value.

adaptive_concurrency.gradient_controller.sample_aggregate_percentile
    Overrides the percentile value used to represent the collection of latency samples in
    calculations. A value of `95` indicates the 95th percentile. The runtime value specified is
    clamped to the range [0,100].

Statistics
----------

