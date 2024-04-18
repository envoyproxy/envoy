.. _config_http_filters_adaptive_concurrency:

Adaptive Concurrency
====================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.adaptive_concurrency.v3.AdaptiveConcurrency``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.AdaptiveConcurrency>` for details on each configuration parameter.

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

:ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig>`

Calculating the minRTT
^^^^^^^^^^^^^^^^^^^^^^

The minRTT is periodically measured by only allowing a very low outstanding request count to an
upstream cluster and measuring the latency under these ideal conditions. The calculation is also
triggered in scenarios where the concurrency limit is determined to be the minimum possible value
for 5 consecutive sampling windows. The length of this minRTT calculation window is variable
depending on the number of requests the filter is configured to aggregate to represent the expected
latency of an upstream.

A configurable *jitter* value is used to randomly delay the start of the minRTT calculation window
by some amount of time. This is not necessary and can be disabled; however, it is recommended to
prevent all hosts in a cluster from being in a minRTT calculation window (and having a concurrency
limit of 3 by default) at the same time. The jitter helps negate the effect of the minRTT
calculation on the downstream success rate if retries are enabled.

It is possible that there is a noticeable increase in request 503s during the minRTT measurement
window because of the potentially significant drop in the concurrency limit. This is expected and it
is recommended to enable retries for resets/503s.

.. note::

    It is recommended to use :ref:`the previous_hosts retry predicate
    <arch_overview_http_retry_plugins>`. Due to the minRTT recalculation jitter, it's unlikely that
    all hosts in the cluster will be in a minRTT calculation window, so retrying on a different host
    in the cluster will have a higher likelihood of success in this scenario.

Once calculated, the minRTT is then used in the calculation of a value referred to as the
*gradient*.

The Gradient
^^^^^^^^^^^^
The gradient is calculated using summarized sampled request latencies (sampleRTT):

.. math::

    gradient = \frac{minRTT + B}{sampleRTT}

This gradient value has a useful property, such that it decreases as the sampled latencies increase.
Notice that *B*, the buffer value added to the minRTT, allows for normal variance in the sampled
latencies by requiring the sampled latencies the exceed the minRTT by some configurable threshold
before decreasing the gradient value.

The buffer will be a percentage of the measured minRTT value whose value is modified via the buffer field in the :ref:`minRTT calculation parameters <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig.MinimumRTTCalculationParams>`. The buffer is calculated as follows:

.. math::

    B = minRTT * buffer_{pct}

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

Because the headroom value is so necessary to the proper function for the gradient controller, the
headroom value is unconfigurable and pinned to the square-root of the concurrency limit.

Limitations
-----------
The adaptive concurrency filter's control loop relies on latency measurements
and adjustments to the concurrency limit based on those measurements. Because of
this, the filter must operate in conditions where it has full control over
request concurrency. This means that:

    1. The filter works as intended in the filter chain for a local cluster.

    2. The filter must be able to limit the concurrency for a cluster. This means
       there must not be requests destined for a cluster that are not decoded by
       the adaptive concurrency filter.

Example Configuration
---------------------
An example filter configuration can be found below. Not all fields are required and many of the
fields can be overridden via runtime settings.

.. code-block:: yaml

  name: envoy.filters.http.adaptive_concurrency
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.adaptive_concurrency.v3.AdaptiveConcurrency
    gradient_controller_config:
      sample_aggregate_percentile:
        value: 90
      concurrency_limit_params:
        concurrency_update_interval: 0.1s
      min_rtt_calc_params:
        jitter:
          value: 10
        interval: 60s
        request_count: 50
    enabled:
      default_value: true
      runtime_key: "adaptive_concurrency.enabled"

The above configuration can be understood as follows:

* Gather latency samples for a time window of 100ms. When entering a new window, summarize the
  requests (sampleRTT) and and update the concurrency limit using this sampleRTT.
* When calculating the sampleRTT, use the p90 of all sampled latencies for that window.
* Recalculate the minRTT every 60s and add a jitter (random delay) of 0s-6s to the start of the
  minRTT recalculation. The delay is dictated by the jitter value.
* Collect 50 request samples to calculate the minRTT and use the p90 to summarize them.
* The filter is enabled by default.

.. note::

    It is recommended that the adaptive concurrency filter come after the healthcheck filter in the
    filter chain to prevent latency sampling of health checks. If health check traffic is sampled,
    it could potentially affect the accuracy of the minRTT measurements.

Runtime
-------

The adaptive concurrency filter supports the following runtime settings:

adaptive_concurrency.enabled
    Overrides whether the adaptive concurrency filter will use the concurrency controller for
    forwarding decisions. If set to ``false``, the filter will be a no-op. Defaults to what is
    specified for ``enabled`` in the filter configuration.

adaptive_concurrency.gradient_controller.min_rtt_calc_interval_ms
    Overrides the interval in which the ideal round-trip time (minRTT) will be recalculated.

adaptive_concurrency.gradient_controller.min_rtt_aggregate_request_count
    Overrides the number of requests sampled for calculation of the minRTT.

adaptive_concurrency.gradient_controller.jitter
    Overrides the random delay introduced to the minRTT calculation start time. A value of ``10``
    indicates a random delay of 10% of the configured interval. The runtime value specified is
    clamped to the range [0,100].

adaptive_concurrency.gradient_controller.sample_rtt_calc_interval_ms
    Overrides the interval in which the concurrency limit is recalculated based on sampled latencies.

adaptive_concurrency.gradient_controller.max_concurrency_limit
    Overrides the maximum allowed concurrency limit.

adaptive_concurrency.gradient_controller.min_rtt_buffer
    Overrides the padding added to the minRTT when calculating the concurrency limit.

adaptive_concurrency.gradient_controller.sample_aggregate_percentile
    Overrides the percentile value used to represent the collection of latency samples in
    calculations. A value of ``95`` indicates the 95th percentile. The runtime value specified is
    clamped to the range [0,100].

adaptive_concurrency.gradient_controller.min_concurrency
    Overrides the concurrency that is pinned while measuring the minRTT.

Statistics
----------
The adaptive concurrency filter outputs statistics in the
*http.<stat_prefix>.adaptive_concurrency.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
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
  gradient, Gauge, The current gradient value multiplied by 1000 (values will range between 500 and 2000).
  burst_queue_size, Gauge, The current headroom value in the concurrency limit calculation.
  min_rtt_msecs, Gauge, The current measured minRTT value.
  sample_rtt_msecs, Gauge, The current measured sampleRTT aggregate.
