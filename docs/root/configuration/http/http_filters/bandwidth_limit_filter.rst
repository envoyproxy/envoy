.. _config_http_filters_bandwidth_limit:

Bandwidth limit
====================

* Bandwidth limiting :ref:`architecture overview <arch_overview_bandwidth_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.bandwidth_limit.v3.BandwidthLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.bandwidth_limit.v3.BandwidthLimit>`

The HTTP Bandwidth limit filter limits the size of data flow to the max bandwidth set in the ``limit_kbps``
when the request's route, virtual host or filter chain has a
:ref:`bandwidth limit configuration <envoy_v3_api_msg_extensions.filters.http.bandwidth_limit.v3.BandwidthLimit>`.

If the bandwidth limit has been exhausted the filter stops further transfer until more bandwidth gets allocated
according to the ``fill_interval`` (default is 50 milliseconds). If the connection buffer fills up with accumulated
data then the source of data will have ``readDisable(true)`` set as described in the :repo:`flow control doc<source/docs/flow_control.md>`.

.. note::
  The token bucket is shared across all workers, thus the limits are applied per Envoy process.

Example configuration
---------------------

Example filter configuration for a globally disabled bandwidth limiter but enabled for a specific route:

.. literalinclude:: _include/bandwidth-limit-filter.yaml
    :language: yaml
    :lines: 11-53
    :emphasize-lines: 9-25
    :caption: :download:`bandwidth-limit-filter.yaml <_include/bandwidth-limit-filter.yaml>`

Note that if this filter is configured as globally disabled and there are no virtual host or route level
token buckets, no bandwidth limiting will be applied.

Statistics
----------

The HTTP bandwidth limit filter outputs statistics in the ``<stat_prefix>.http_bandwidth_limit.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request_enabled, Counter, Total number of request streams for which the bandwidth limiter was consulted
  request_enforced, Counter, Total number of request streams for which the bandwidth limiter was enforced
  request_pending, GAUGE, Number of request streams which are currently pending transfer in bandwidth limiter
  request_incoming_size, GAUGE, Size in bytes of incoming request data to bandwidth limiter
  request_allowed_size, GAUGE, Size in bytes of outgoing request data from bandwidth limiter
  request_transfer_duration, HISTOGRAM, Total time (including added delay) it took for the request stream transfer
  response_enabled, Counter, Total number of response streams for which the bandwidth limiter was consulted
  response_enforced, Counter, Total number of response streams for which the bandwidth limiter was enforced
  response_pending, GAUGE, Number of response streams which are currently pending transfer in bandwidth limiter
  response_incoming_size, GAUGE, Size in bytes of incoming response data to bandwidth limiter
  response_allowed_size, GAUGE, Size in bytes of outgoing response data from bandwidth limiter
  response_transfer_duration, HISTOGRAM, Total time (including added delay) it took for the response stream transfer

.. _config_http_filters_bandwidth_limit_runtime:

Runtime
-------

The HTTP bandwidth limit filter supports the following runtime settings:

The bandwidth limit filter can be runtime feature flagged via the :ref:`enabled
<envoy_v3_api_field_extensions.filters.http.bandwidth_limit.v3.BandwidthLimit.runtime_enabled>`
configuration field.
