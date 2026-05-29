.. _config_network_filters_tcp_bandwidth_limit:

TCP bandwidth limit
===================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit>`

Overview
--------

The TCP bandwidth limit filter is a network filter that limits the bandwidth on the downstream
connection. It can be configured to limit read and write bandwidth independently, using a token
bucket algorithm similar to the HTTP bandwidth limit filter.

- ``read_limit_kbps`` limits data read from the downstream connection.
- ``write_limit_kbps`` limits data written to the downstream connection.

The filter works by:

* Consuming tokens from a token bucket when data passes through
* Buffering data when insufficient tokens are available
* Using timers to refill tokens and resume data flow when bandwidth becomes available

Example configuration
---------------------

The following example configuration limits read bandwidth to 1 MiB/s and write bandwidth to 512 KiB/s:

.. code-block:: yaml

  name: envoy.filters.network.tcp_bandwidth_limit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
    stat_prefix: bandwidth_limiter
    read_limit_kbps: 1024   # 1 MiB/s
    write_limit_kbps: 512   # 512 KiB/s
    fill_interval:
      nanos: 50000000       # 50ms

Statistics
----------

The TCP bandwidth limit filter outputs statistics in the ``<stat_prefix>.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  read_enabled, Counter, Total number of times the read limit was applied to incoming data
  write_enabled, Counter, Total number of times the write limit was applied to outgoing data
  read_throttled, Counter, Total number of times read data was throttled
  write_throttled, Counter, Total number of times write data was throttled
  read_total_bytes, Counter, Total bytes read
  write_total_bytes, Counter, Total bytes written
  read_bytes_buffered, Gauge, Current number of bytes buffered for read
  write_bytes_buffered, Gauge, Current number of bytes buffered for write
  read_rate_bps, Gauge, Current read rate in bytes per second
  write_rate_bps, Gauge, Current write rate in bytes per second

Runtime
-------

The TCP bandwidth limit filter can be runtime feature flagged via the :ref:`runtime_enabled
<envoy_v3_api_field_extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit.runtime_enabled>`
configuration field.
