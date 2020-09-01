.. _config_network_filters_redis_proxy:

Redis proxy
===========

* Redis :ref:`architecture overview <arch_overview_redis>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.redis_proxy.v3.RedisProxy>`
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
changed to microseconds by setting the configuration parameter :ref:`latency_in_micros <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.latency_in_micros>` to true.

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
---------------

The Redis filter can perform fault injection. Currently, Delay and Error faults are supported.
Delay faults delay a request, and Error faults respond with an error. Moreover, errors can be delayed.

Note that the Redis filter does not check for correctness in your configuration - it is the user's
responsibility to make sure both the default and runtime percentages are correct! This is because
percentages can be changed during runtime, and validating correctness at request time is expensive.
If multiple faults are specified, the fault injection percentage should not exceed 100% for a given 
fault and Redis command combination. For example, if two faults are specified; one applying to GET at 60
%, and one applying to all commands at 50%, that is a bad configuration as GET now has 110% chance of
applying a fault. This means that every request will have a fault.

If a delay is injected, the delay is additive - if the request took 400ms and a delay of 100ms
is injected, then the total request latency is 500ms. Also, due to implementation of the redis protocol,
a delayed request will delay everything that comes in after it, due to the proxy's need to respect the 
order of commands it receives.

Note that faults must have a `fault_enabled` field, and are not enabled by default (if no default value
or runtime key are set).

Example configuration:

.. code-block:: yaml

  faults:
  - fault_type: ERROR
    fault_enabled:
      default_value:
        numerator: 10
        denominator: HUNDRED
      runtime_key: "bogus_key"
      commands:
      - GET
    - fault_type: DELAY
      fault_enabled:
        default_value:
          numerator: 10
          denominator: HUNDRED
        runtime_key: "bogus_key"
      delay: 2s

This creates two faults- an error, applying only to GET commands at 10%, and a delay, applying to all
commands at 10%. This means that 20% of GET commands will have a fault applied, as discussed earlier.
