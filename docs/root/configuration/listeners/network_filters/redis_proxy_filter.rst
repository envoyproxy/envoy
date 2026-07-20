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
  downstream_rq_noproto, Counter, "Data commands rejected ``-NOPROTO`` for arriving before the ``HELLO 3`` handshake on a ``protocol_version: RESP3`` listener"
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

.. _config_network_filters_redis_proxy_protocol_version:

RESP protocol version
---------------------

The Redis proxy filter speaks one RESP protocol version on the listener, configured via the
:ref:`protocol_version
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.protocol_version>`
field on :ref:`RedisProxy
<envoy_v3_api_msg_extensions.filters.network.redis_proxy.v3.RedisProxy>`. The same value
governs both downstream client connections and every routed upstream connection pool — there
is no separate per-cluster RESP knob, and no implicit floor across clusters. On the
upstream-routed data path, downstream and upstream speak the same RESP version (locally
emitted replies — AUTH/QUIT/NOPROTO — are encoded in the downstream-negotiated version).

When ``protocol_version`` is unset or ``RESP2`` (the default), the negotiated wire version is
RESP2: no ``HELLO 3`` is sent upstream, and a downstream ``HELLO 3`` is rejected with
``-NOPROTO``. (RESP3-aware handling — the local ``HELLO`` reply, ``CLIENT SETINFO`` /
``SETNAME`` acceptance, and the RESP3 decoder — is always present regardless of this value.)

When ``protocol_version`` is ``RESP3``:

* Every routed upstream Redis-compatible backend must support ``HELLO 3`` / RESP3
  (Redis 6.0+, where RESP3 was introduced). Misconfigured upstreams fail every connection's
  HELLO 3 negotiation, surfaced as ``upstream_resp3_hello_failure`` counter increments on
  the per-cluster scope.
* The upstream client sends ``HELLO 3`` (combined with ``AUTH`` when static credentials or
  AWS IAM authentication are configured) on every new upstream connection. User requests
  submitted before negotiation completes are buffered and replayed in order once both
  ``HELLO`` and any required ``READONLY`` (for non-Primary read policies) succeed. If the
  upstream rejects RESP3, the connection is closed and the buffered requests fail upstream
  so the caller can retry on a fresh connection that will re-attempt negotiation.
* Downstream clients must perform an explicit ``HELLO 3`` handshake before any data command.
  Any command other than ``HELLO``, ``AUTH``, or ``QUIT`` arriving on a connection that has
  not yet negotiated RESP3 is rejected with ``-NOPROTO`` — including unknown commands, which
  surface as ``-NOPROTO`` rather than the splitter's usual ``ERR unknown command`` so the
  operator-facing error always points to "client failed to handshake" rather than masking
  the missing handshake.
* Both explicit ``HELLO N`` and bare ``HELLO`` are exact-matched against the listener's
  ``protocol_version``: bare ``HELLO`` on a fresh ``RESP3`` listener is rejected because the
  connection's current version (default ``2``) does not match the required ``3``. After a
  successful ``HELLO 3``, bare ``HELLO`` reaffirms the negotiated version.

Downstream ``HELLO N AUTH <user> <pass>`` is supported with both locally configured
credentials (``downstream_auth_passwords`` / ``downstream_auth_username``) and an external
auth provider; the latter defers the round trip and emits the deferred ``HELLO`` Map (or
error) when the provider responds.

When a ``HELLO`` is resolved by an external auth provider, its outcome is emitted from the
filter after the deferred round trip, so only ``command.hello.total`` is incremented —
``command.hello.success`` and ``command.hello.error`` are not. For ``HELLO`` alone, therefore,
``total`` may exceed ``success + error``; the authentication result stays observable through
the external auth provider's own metrics and the downstream reply.

The ``HELLO`` reply returned to the downstream client is **synthesized locally** by the proxy;
it is not proxied from, and does not reflect, any upstream Redis server. Several fields therefore
carry fixed proxy-specific values rather than a backend's: ``server`` is ``envoy-redis-proxy``,
``version`` is a fixed Redis-compatibility version (``6.0.0``) advertised for client-library
compatibility rather than the Envoy build version, ``id`` is ``0``, ``mode`` is ``standalone``,
``role`` is ``master``, and ``modules`` is empty. Only ``proto`` is dynamic — it reflects the
negotiated version (``2`` or ``3``). Clients that key behavior off these fields (for example a
server ``version`` gate or the connection ``id``) must not expect them to match the upstream
Redis the data commands are routed to.

The proxy does not cross-encode upstream responses between RESP2 and RESP3. Because the
listener forces the upstream-routed data path to a single RESP version, an upstream reply
is always emitted downstream in the same RESP version it arrived; no transparent reshaping
(e.g. RESP3 Map → flat RESP2 array) is attempted, which avoids structural divergence such
as ``ZRANGE WITHSCORES`` returning nested pair arrays under RESP3 vs flat arrays under
RESP2.

The one exception is cluster-scoped commands whose replies are aggregated across shards
(for example ``CONFIG GET`` and ``KEYS`` on a Redis Cluster): these are always emitted as a
flat array, even when a RESP3 upstream shard returns a Map. Aggregating shard responses into
a Map would force clients into duplicate-key handling across shards, so a stable flat array
is emitted for both RESP2 and RESP3 downstreams.

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
additional fields are provided in the cluster Redis settings.
If `region` is not specified, the region will be deduced using the region provider chain as described in  :ref:`config_http_filters_aws_request_signing_region`.
`cache_name` is required and is set to the name of your cache. Both `auth_username` and `cache_name` are used when calculating the IAM authentication token.
`auth_password` is not used in AWS IAM configuration and the password value is automatically calculated by Envoy.
In your upstream cluster, the `auth_username` field must be configured with the user that has been added to your cache, as per
`Setup <https://docs.aws.amazon.com/AmazonElastiCache/latest/dg/auth-iam.html#auth-iam-setup>`_. Different upstreams may use different usernames and different
cache names, credentials will be generated correctly based on the cluster the traffic is destined to.
The `service_name` should be `elasticache` for an Amazon ElastiCache cache in valkey or Redis OSS mode, or `memorydb` for an Amazon MemoryDB cluster. The `service_name`
matches the service which is added to the IAM Policy for the associated IAM principal being used to make the connection. For example, `service_name: memorydb` matches
an AWS IAM Policy containing the Action `memorydb:Connect`, and that policy must be attached to the IAM principal being used by Envoy.

.. literalinclude:: _include/redis-aws-iam-auth.yaml
   :language: yaml
   :lines: 8-41
   :linenos:
   :lineno-start: 8
   :caption: :download:`redis-aws-iam-auth.yaml <_include/redis-aws-iam-auth.yaml>`
