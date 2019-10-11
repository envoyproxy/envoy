.. _config_http_filters_adaptive_concurrency:

Adaptive Concurrency
====================

.. attention::

  The adaptive concurrency filter is experimental and is currently under active development.

This filter should be configured with the name `envoy.filters.http.adaptive_concurrency`.

:ref:`v2 API reference <envoy_api_msg_config.filter.http.adaptive_concurrency.v2alpha.AdaptiveConcurrency>`

Overview
--------
The adaptive concurrency filter dynamically adjusts the allowed number of requests that can be
outstanding (concurrency) to all hosts in a given cluster at any time. Concurrency values are
calculated using latency sampling of completed requests and comparing the measured samples in a time
window against the expected latency for hosts in the cluster.

Concurrency Controllers
-----------------------
Concurrency controllers implement the algorithm responsible for making forwarding decisions for each
request and recording latency samples to use in the calculation of the concurrency limit.

Gradient Controller
~~~~~~~~~~~~~~~~~~~
The gradient controller makes forwarding decisions based on a periodically measured ideal round-trip
time (minRTT) for an upstream.

:ref:`v2 API reference <envoy_api_msg_config.filter.http.adaptive_concurrency.v2alpha.GradientControllerConfig>`

Calculating the minRTT
^^^^^^^^^^^^^^^^^^^^^^

The minRTT is periodically measured by only allowing a single
outstanding request at a time and measuring the latency under these ideal conditions. The length of
this minRTT calculation window is variable depending on the number of requests the filter is
configured to aggregate to represent the expected latency of an upstream.

.. note::

    It is possible that there is a noticeable increase in request 503s during the minRTT
    measurement window because of a significant drop in the concurrency limit. This is expected and
    it is recommended to use :ref:`the previous_hosts retry predicate <arch_overview_http_retry_plugins>` to avoid unexpected synchonization of upstream hosts configured with the adaptive concurrency filter.

The calculated minRTT is then used in the calculation of a value referred to as the *gradient*.

The Gradient
^^^^^^^^^^^^
The gradient is calculated using summarized sampled request latencies (sampleRTT):

.. math::

    gradient = \frac{minRTT}{sampleRTT}

This gradient value has a useful property, such that it decreases as the sampled latencies increase.
The gradient value is then used to update the concurrency limit via:

.. math::

    limit_{new} = gradient * limit_{old} + headroom

Concurrency Limit Headroom
^^^^^^^^^^^^^^^^^^^^^^^^^^
The headroom value is necessary as a driving factor to increase the concurrency limit when the
sampleRTT is in the same ballpark as the minRTT. This value must be present in the limit
calculation, since it forces the concurrency limit to increase until there is a deviation from the
minRTT latency. In the absence of a headroom value, the concurrency limit could potentially stagnate
at an unnecessary small value if the sampleRTT and minRTT are close to each other.

Because the headroom value is so necessary to the proper function fo the gradient controller, the
headroom value is unconfigurable and pinned to the square-root of the concurrency limit.

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
The adaptive concurrency filter outputs statistics in the
*http.<stat_prefix>.adaptive_concurrency.* namespace. The :ref:`stat prefix
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager. Statistics are specific to the concurrency
controllers.

Gradient Controller Statistics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The gradient controller uses the namespace
*http.<stat_prefix>.adaptive_concurrency.gradient_controller*.

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  rq_blocked, Counter, Total requests that were blocked by the filter.
  min_rtt_calculation_active, Gauge, Set to 1 if the controller is in the process of a minRTT calculation. 0 otherwise.
  concurrency_limit, Gauge, The current concurrency limit.
  gradient, Gauge, The current gradient value.
  burst_queue_size, Gauge, The current headroom value in the concurrency limit calculation.
  min_rtt_msecs, Gauge, The current measured minRTT value.
  sample_rtt_msecs, Gauge, The current measured sampleRTT aggregate.
