.. _extension_envoy.load_balancing_policies.peak_ewma:
.. _extension_envoy.filters.http.peak_ewma:

Peak EWMA Load Balancer
========================

* This load balancer should be configured with the type URL ``type.googleapis.com/envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma``.

.. note::

  Peak EWMA is a contrib extension that must be explicitly enabled at Envoy build time.
  See :ref:`install_contrib` for details.

The Peak EWMA (Exponentially Weighted Moving Average) load balancer implements a latency-
and active-requests-aware variant of the Power of Two Choices (P2C) algorithm. It automatically
routes traffic to the best-performing hosts based on real-time latency measurements and
outstanding active requests.

Peak EWMA considers more input data than Envoy's other load balancing algorithms, which enables
it to make superior routing decisions. In the case of a slowdown, it seamlessly moves traffic
away from the affected host.

Peak EWMA is also well-suited for cross-data-center routing: it naturally prefers upstream
hosts in the closest data center, but seamlessly fails over to other data centers during
slowdowns (and fails back when performance recovers).

In scenarios where all upstream hosts have similar request latency, Peak EWMA behaves
equivalently to equal-weighted least request load balancing (using P2C selection).

.. note::

  Peak EWMA requires both the load balancing policy AND the HTTP filter to function properly.
  The HTTP filter (``envoy.filters.http.peak_ewma``) measures request RTT and provides timing
  data to the load balancer. Without the HTTP filter, the load balancer cannot collect
  latency measurements.

  Note: This requirement may change if Peak EWMA becomes a core Envoy load balancing algorithm
  in the future, which would require core changes to integrate RTT measurement functionality.

.. important::

  Peak EWMA considers latency and load when making routing decisions. It does **not** handle
  unhealthy hosts or error responses directly. This is especially critical because upstream hosts
  that fast-fail (return errors quickly) may appear to have low latency, causing Peak EWMA to send
  them a greater proportion of traffic — exactly the opposite of what you want.

  Always configure Envoy's :ref:`health checking <arch_overview_health_checking>` and
  :ref:`outlier detection <arch_overview_outlier_detection>` to automatically remove failing
  hosts from the load balancing pool before Peak EWMA makes routing decisions.

Algorithm Overview
------------------

Peak EWMA uses the cost function: ``Cost = RTT_peak_ewma * (active_requests + 1)``

Key characteristics:

* **Latency-sensitive**: Automatically de-prioritizes slow hosts
* **Load-aware**: Considers both latency and current request count
* **O(1) complexity**: Efficient P2C selection scales to large clusters
* **Adaptive**: No manual tuning required, responds to performance changes
* **Health-agnostic**: Operates only on healthy hosts as determined by health checking and outlier detection

Integration with Health Management
----------------------------------

Peak EWMA works in conjunction with Envoy's health management systems:

* **Health Checking**: Only hosts that pass active health checks are considered for load balancing
* **Outlier Detection**: Hosts ejected by outlier detection are automatically excluded from selection
* **Error Handling**: HTTP error responses (4xx/5xx) do not directly affect Peak EWMA routing decisions

For comprehensive host health management, configure Peak EWMA alongside:

.. code-block:: yaml

  cluster:
    # Health checking removes unresponsive hosts
    health_checks:
    - timeout: 5s
      interval: 10s
      http_health_check:
        path: "/health"

    # Outlier detection removes hosts with high error rates
    outlier_detection:
      consecutive_5xx: 3
      interval: 30s
      base_ejection_time: 30s

    # Peak EWMA optimizes among remaining healthy hosts
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.peak_ewma
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma
            decay_time: 10s

  # HTTP filter configuration - required for RTT measurement
  http_filters:
  - name: envoy.filters.http.peak_ewma
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.peak_ewma.v3alpha.PeakEwmaConfig

Configuration Parameters
------------------------

Peak EWMA supports the following configuration parameters:

**decay_time** (``google.protobuf.Duration``, default: 10s)
  The time window over which latency observations decay to half their original weight.
  Shorter values adapt faster to performance changes, longer values provide more stability.

**aggregation_interval** (``google.protobuf.Duration``, default: 100ms)
  Frequency of EWMA data aggregation from worker threads. Lower values provide fresher
  data but increase CPU overhead.

**max_samples_per_host** (``google.protobuf.UInt32Value``, default: 1,000)
  Ring buffer size per host per worker thread for RTT samples. Larger values handle
  traffic bursts better but consume more memory.

  Buffer capacity = max_samples_per_host / aggregation_interval = RPS capacity per host per worker.

**default_rtt** (``google.protobuf.Duration``, default: 10ms)
  Baseline RTT for cost calculations when no measurements are available yet. Should
  reflect expected latency in your environment.

**penalty_value** (``google.protobuf.DoubleValue``, default: 1,000,000.0)
  Cost penalty for hosts without RTT data. You probably should not change this value.

Example configuration
---------------------

**Minimal configuration** with defaults suitable for most deployments:

.. code-block:: yaml

  cluster:
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.peak_ewma
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma

  # HTTP filter configuration - required for RTT measurement
  http_filters:
  - name: envoy.filters.http.peak_ewma
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.peak_ewma.v3alpha.PeakEwmaConfig

**Complete configuration** showing all available parameters:

.. code-block:: yaml

  cluster:
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.peak_ewma
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma
            decay_time: 10s
            aggregation_interval: 100ms
            max_samples_per_host: 1000
            default_rtt: 10ms
            penalty_value: 1000000.0

Statistics
----------

The Peak EWMA load balancer outputs statistics in the ``cluster.<cluster_name>.peak_ewma.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  samples_recorded, Counter, Total RTT samples recorded across all hosts
  samples_dropped, Counter, Samples dropped due to buffer overflow
  ewma_calculations, Counter, Number of EWMA calculations performed
  hosts_with_data, Gauge, Number of hosts with available EWMA data
  aggregation_cycles, Counter, Number of aggregation timer cycles executed

Performance Characteristics
---------------------------

Peak EWMA provides the following performance characteristics:

* **Selection complexity**: O(1) per request using Power of Two Choices algorithm
* **Memory usage**: Configurable via ``max_samples_per_host`` parameter
* **CPU overhead**: Minimal during request processing, periodic aggregation every 100ms

The load balancer maintains constant selection time regardless of cluster size.

API Reference
-------------

The Peak EWMA load balancing policy is configured using the
``envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma`` proto message.
