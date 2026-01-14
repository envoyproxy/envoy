.. _config_network_filters_redis_proxy:

Redis proxy
===========

* Redis :ref:`architecture overview <arch_overview_redis>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.redis_proxy.v3.RedisProxy>`

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

Note that faults must have a ``fault_enabled`` field, and are not enabled by default (if no default value
or runtime key are set).

Example configuration:

.. literalinclude:: _include/redis-fault-injection.yaml
   :language: yaml
   :lines: 19-34
   :linenos:
   :lineno-start: 19
   :caption: :download:`redis-fault-injection.yaml <_include/redis-fault-injection.yaml>`

This creates two faults- an error, applying only to GET commands at 10%, and a delay, applying to all
commands at 10%. This means that 20% of GET commands will have a fault applied, as discussed earlier.

DNS lookups on redirections
---------------------------

As noted in the :ref:`architecture overview <arch_overview_redis>`, when Envoy sees a MOVED or ASK response containing a hostname it will not perform a DNS lookup and instead bubble up the error to the client. The following configuration example enables DNS lookups on such responses to avoid the client error and have Envoy itself perform the redirection:

.. literalinclude:: _include/redis-dns-lookups.yaml
   :language: yaml
   :lines: 11-23
   :linenos:
   :lineno-start: 11
   :caption: :download:`redis-dns-lookups.yaml <_include/redis-dns-lookups.yaml>`

.. _config_network_filters_redis_proxy_upstream_auth:

Upstream Redis Authentication
-----------------------------

The Redis proxy filter supports authenticating to upstream Redis clusters. If there are multiple upstream clusters configured, they can use either the same
username and password or separate ones per cluster if each credential can be linked to the relevant cluster, and the proxy filter will authenticate
appropriately to them.

To use the same username and password for all upstream clusters, the top-level `auth_username` and `auth_password` in `RedisProtocolOptions` should be used.

To use separate credentials for each upstream cluster, then the top-level `credentials` field in `RedisProtocolOptions` should be used. The `address` field
is used to link this credential to individual upstream endpoint in `load_assignment.endpoints.lb_endpoints.endpoint`. The values for the `address` in both
locations should be the same. Only socket addresses are supported in this mode.

.. literalinclude:: _include/redis-upstream-auth.yaml
    :language: yaml
    :lines: 19-73
    :linenos:
    :lineno-start: 19
    :caption: :download:`redis-upstream-auth.yaml <_include/redis-upstream-auth.yaml>`

.. _config_network_filters_redis_proxy_aws_iam:

AWS IAM Authentication
----------------------

The redis proxy filter supports authentication with AWS IAM credentials, to ElastiCache and MemoryDB instances. To configure AWS IAM Authentication,
additional fields are provided in the cluster redis settings.
If `region` is not specified, the region will be deduced using the region provider chain as described in  :ref:`config_http_filters_aws_request_signing_region`.
`cache_name` is required and is set to the name of your cache. Both `auth_username` and `cache_name` are used when calculating the IAM authentication token.
`auth_password` is not used in AWS IAM configuration and the password value is automatically calculated by envoy.
In your upstream cluster, the `auth_username` field must be configured with the user that has been added to your cache, as per
`Setup <https://docs.aws.amazon.com/AmazonElastiCache/latest/dg/auth-iam.html#auth-iam-setup>`_. Different upstreams may use different usernames and different
cache names, credentials will be generated correctly based on the cluster the traffic is destined to.
The `service_name` should be `elasticache` for an Amazon ElastiCache cache in valkey or Redis OSS mode, or `memorydb` for an Amazon MemoryDB cluster. The `service_name`
matches the service which is added to the IAM Policy for the associated IAM principal being used to make the connection. For example, `service_name: memorydb` matches
an AWS IAM Policy containing the Action `memorydb:Connect`, and that policy must be attached to the IAM principal being used by envoy.

.. literalinclude:: _include/redis-aws-iam-auth.yaml
   :language: yaml
   :lines: 8-41
   :linenos:
   :lineno-start: 8
   :caption: :download:`redis-aws-iam-auth.yaml <_include/redis-aws-iam-auth.yaml>`
