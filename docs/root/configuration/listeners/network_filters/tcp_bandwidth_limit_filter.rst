.. _config_network_filters_tcp_bandwidth_limit:

TCP bandwidth limit
===================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit>`

Overview
--------

The TCP bandwidth limit filter is a network filter that limits the bandwidth of TCP connections.
It can be configured to limit download (ingress) and upload (egress) bandwidth independently,
using a token bucket algorithm similar to the HTTP bandwidth limit filter.

The filter works by:

* Consuming tokens from a token bucket when data passes through
* Buffering data when insufficient tokens are available
* Using timers to refill tokens and resume data flow when bandwidth becomes available

Example configuration
---------------------

The following example configuration limits download bandwidth to 1 MiB/s and upload bandwidth to 512 KiB/s:

.. code-block:: yaml

  name: envoy.filters.network.tcp_bandwidth_limit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
    stat_prefix: bandwidth_limiter
    download_limit_kbps: 1024  # 1 MiB/s
    upload_limit_kbps: 512     # 512 KiB/s
    fill_interval:
      nanos: 50000000          # 50ms

Statistics
----------

The TCP bandwidth limit filter outputs statistics in the ``<stat_prefix>.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  download_throttled, Counter, Total number of times download data was throttled
  upload_throttled, Counter, Total number of times upload data was throttled
  download_bytes_buffered, Gauge, Current number of bytes buffered for download
  upload_bytes_buffered, Gauge, Current number of bytes buffered for upload

Runtime
-------

The TCP bandwidth limit filter supports the following runtime settings:

* ``<stat_prefix>.runtime_enabled``: Percentage of connections for which the filter is enabled.
  Default is 100% if runtime_enabled configuration is not specified.
