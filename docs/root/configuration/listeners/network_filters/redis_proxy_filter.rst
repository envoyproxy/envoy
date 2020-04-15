.. _config_network_filters_redis_proxy:

Redis proxy
===========

* Redis :ref:`architecture overview <arch_overview_redis>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.network.redis_proxy.v2.RedisProxy>`
* This filter should be configured with the name *envoy.filters.network.redis_proxy*.

.. _config_network_filters_redis_proxy_stats:

Statistics
----------

Every configured Redis proxy filter has statistics rooted at *redis.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_cx_active, Gauge, Total active connections
  downstream_cx_protocol_error, Counter, Total protocol errors
  downstream_cx_rx_bytes_buffered, Gauge, Total received bytes currently buffered
  downstream_cx_rx_bytes_total, Counter, Total bytes received
  downstream_cx_total, Counter, Total connections
  downstream_cx_tx_bytes_buffered, Gauge, Total sent bytes currently buffered
  downstream_cx_tx_bytes_total, Counter, Total bytes sent
  downstream_cx_drain_close, Counter, Number of connections closed due to draining
  downstream_rq_active, Gauge, Total active requests
  downstream_rq_total, Counter, Total requests


Splitter statistics
-------------------

The Redis filter will gather statistics for the command splitter in the
*redis.<stat_prefix>.splitter.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  invalid_request, Counter, Number of requests with an incorrect number of arguments
  unsupported_command, Counter, Number of commands issued which are not recognized by the command splitter

Per command statistics
----------------------

The Redis filter will gather statistics for commands in the
*redis.<stat_prefix>.command.<command>.* namespace. By default latency stats are in milliseconds and can be
changed to microseconds by setting the configuration parameter :ref:`latency_in_micros <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.latency_in_micros>` to true.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Number of commands
  success, Counter, Number of commands that were successful
  error, Counter, Number of commands that returned a partial or complete error response
  latency, Histogram, Command execution time in milliseconds (including delay faults)
  error_fault, Counter, Number of commands that had an error fault injected
  delay_fault, Counter, Number of commands that had a delay fault injected
  
.. _config_network_filters_redis_proxy_per_command_stats:

Runtime
-------

The Redis proxy filter supports the following runtime settings:

redis.drain_close_enabled
  % of connections that will be drain closed if the server is draining and would otherwise
  attempt a drain close. Defaults to 100.

.. _config_network_filters_redis_proxy_fault_injection:

Fault Injection
-------------

The Redis filter can perform fault injection. Currently, Delay and Error faults are supported.
Delay faults delay a request, and Error faults respond with an error. Moreover, errors can be delayed.

Note that if a delay is injected, the delay is additive- if the request took 400ms and a delay of 100ms
is injected, then the total request latency is 500ms. Also, due to implementation of the redis proxy,
a delayed request will delay everything that comes in after it, due to the proxy's need to respect the 
order of commands it receives.