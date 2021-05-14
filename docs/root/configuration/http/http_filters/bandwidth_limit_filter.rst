.. _config_http_filters_bandwidth_limit:

HTTP Bandwidth limit
====================

* Bandwidth limiting :ref:`architecture overview <arch_overview_bandwidth_limit>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit>`
* This filter should be configured with the name *envoy.filters.http.bandwidth_limit*.

The HTTP Bandwidth limit filter limits the size of data flow to the max bandwidth set in the limit_kbps
when the request's route or virtual host has a per filter
:ref:`bandwidth limit configuration <envoy_v3_api_msg_extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit>`.

If the bandwidth limit has been exhausted the filter stops further transfer until more bandwidth gets allocated
according to the fill_interval(default is 50 milliseconds). If the connection buffer fills up with accumulated
data then the source of data will be readDisabled as described in the :ref:`flow control doc<faq_flow_control>`.

.. note::
  The token bucket is shared across all workers, thus the limits are applied per Envoy process.

Example configuration
---------------------

Example filter configuration for a globally set bandwidth limiter (e.g.: all vhosts/routes share the same token bucket):

.. code-block:: yaml

  name: envoy.filters.http.bandwidth_limit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit
    stat_prefix: http_bandwidth_limiter
    enable_mode: DecodeAndEncode
    limit_kbps: 100
    fill_interval: 0.1s

Example filter configuration for a globally disabled bandwidth limiter but enabled for a specific route:

.. code-block:: yaml

  name: envoy.filters.http.bandwidth_limit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit
    stat_prefix: http_bandwidth_limiter


The route specific configuration:

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/path/with/bandwidth/limit" }
        route: { cluster: service_protected_by_bandwidth_limit }
        typed_per_filter_config:
          envoy.filters.http.bandwidth_limit:
            "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit
            enable_mode: DecodeAndEncode
            limit_kbps: 500
            fill_interval: 1s
      - match: { prefix: "/" }
        route: { cluster: default_service }


Note that if this filter is configured as globally disabled and there are no virtual host or route level
token buckets, no bandwidth limiting will be applied.

Statistics
----------

The HTTP bandwidth limit filter outputs statistics in the *<stat_prefix>.http_bandwidth_limit.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decode_enabled, Counter, Total number of request streams for which the bandwidth limiter was consulted
  decode_pending, GAUGE, Number of request streams which the currently pending transfer in bandwidth limiter
  decode_incoming_size, GAUGE, Size in bytes of incoming request data to bandwidth limiter
  decode_allowed_size, GAUGE, Size in bytes of outgoing request data from bandwidth limiter
  decode_transfer_duration, HISTOGRAM, Total time (including added delay) it took for the request stream transfer
  encode_enabled, Counter, Total number of response streams for which the bandwidth limiter was consulted
  encode_pending, GAUGE, Number of response streams which the currently pending transfer in bandwidth limiter
  encode_incoming_size, GAUGE, Size in bytes of incoming response data to bandwidth limiter
  encode_allowed_size, GAUGE, Size in bytes of outgoing response data from bandwidth limiter
  encode_transfer_duration, HISTOGRAM, Total time (including added delay) it took for the response stream transfer

.. _config_http_filters_bandwidth_limit_runtime:

Runtime
-------

The HTTP bandwidth limit filter supports the following runtime settings:

The bandwidth limit filter can be runtime feature flagged via the :ref:`enabled
<envoy_v3_api_field_extensions.filters.http.bandwidth_limit.v3alpha.BandwidthLimit.runtime_enabled>`
configuration field.
