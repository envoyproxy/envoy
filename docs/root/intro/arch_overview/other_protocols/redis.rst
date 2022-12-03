.. _arch_overview_redis:

Redis
=======

Envoy can act as a Redis proxy, partitioning commands among instances in a cluster.
In this mode, the goals of Envoy are to maintain availability and partition tolerance
over consistency. This is the key point when comparing Envoy to `Redis Cluster
<https://redis.io/topics/cluster-spec>`_. Envoy is designed as a best-effort cache,
meaning that it will not try to reconcile inconsistent data or keep a globally consistent
view of cluster membership. It also supports routing commands from different workloads to
different upstream clusters based on their access patterns, eviction, or isolation
requirements.

The Redis project offers a thorough reference on partitioning as it relates to Redis. See
"`Partitioning: how to split data among multiple Redis instances
<https://redis.io/topics/partitioning>`_".

**Features of Envoy Redis**:

* `Redis protocol <https://redis.io/topics/protocol>`_ codec.
* Hash-based partitioning.
* Redis transaction support.
* Ketama distribution.
* Detailed command statistics.
* Active and passive healthchecking.
* Hash tagging.
* Prefix routing.
* Separate downstream client and upstream server authentication.
* Request mirroring for all requests or write requests only.
* Control :ref:`read requests routing<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.read_policy>`. This only works with Redis Cluster.

**Planned future enhancements**:

* Additional timing stats.
* Circuit breaking.
* Request collapsing for fragmented commands.
* Replication.
* Built-in retry.
* Tracing.

.. _arch_overview_redis_configuration:

Configuration
-------------

For filter configuration details, see the Redis proxy filter
:ref:`configuration reference <config_network_filters_redis_proxy>`.

The corresponding cluster definition should be configured with
:ref:`ring hash load balancing <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>`.

If :ref:`active health checking <arch_overview_health_checking>` is desired, the
cluster should be configured with a :ref:`custom health check
<envoy_v3_api_field_config.core.v3.HealthCheck.custom_health_check>` which configured as a
:ref:`Redis health checker <config_health_checkers_redis>`.

If passive healthchecking is desired, also configure
:ref:`outlier detection <arch_overview_outlier_detection>`.

For the purposes of passive healthchecking, connect timeouts, command timeouts, and connection
close map to 5xx. All other responses from Redis are counted as a success.

.. _arch_overview_redis_cluster_support:

Redis Cluster Support
---------------------

Envoy offers support for `Redis Cluster <https://redis.io/topics/cluster-spec>`_.

When using Envoy as a sidecar proxy for a Redis Cluster, the service can use a non-cluster Redis client
implemented in any language to connect to the proxy as if it's a single node Redis instance.
The Envoy proxy will keep track of the cluster topology and send commands to the correct Redis node in the
cluster according to the `spec <https://redis.io/topics/cluster-spec>`_. Advance features such as reading
from replicas can also be added to the Envoy proxy instead of updating redis clients in each language.

Envoy proxy tracks the topology of the cluster by sending periodic
`cluster slots <https://redis.io/commands/cluster-slots>`_ commands to a random node in the cluster, and maintains the
following information:

* List of known nodes.
* The primaries for each shard.
* Nodes entering or leaving the cluster.

Envoy proxy supports identification of the nodes via both IP address and hostnames in the ``cluster slots`` command response. In case of failure to resolve a primary hostname, Envoy will retry resolution of all nodes periodically until success. Failure to resolve a replica simply skips that replica. On the other hand, if the :ref:`enable_redirection <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.enable_redirection>` option is set and a MOVED or ASK response containing a hostname is received Envoy will not automatically do a DNS lookup and instead bubble the error to the client verbatim. To have Envoy do the DNS lookup and follow the redirection, you need to configure the DNS cache option :ref:`dns_cache_config <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.dns_cache_config>` under the connection pool settings. For a configuration example on how to enable DNS lookups for redirections, see the filter :ref:`configuration reference <config_network_filters_redis_proxy>`.

For topology configuration details, see the Redis Cluster
:ref:`v3 API reference <envoy_v3_api_msg_extensions.clusters.redis.v3.RedisClusterConfig>`.

Every Redis cluster has its own extra statistics tree rooted at *cluster.<name>.redis_cluster.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  max_upstream_unknown_connections_reached, Counter, Total number of times that an upstream connection to an unknown host is not created after redirection having reached the connection pool's max_upstream_unknown_connections limit
  upstream_cx_drained, Counter, Total number of upstream connections drained of active requests before being closed
  upstream_commands.upstream_rq_time, Histogram, Histogram of upstream request times for all types of requests

.. _arch_overview_redis_cluster_command_stats:

Per-cluster command statistics can be enabled via the setting :ref:`enable_command_stats <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.enable_command_stats>`.:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_commands.[command].success, Counter, Total number of successful requests for a specific Redis command
  upstream_commands.[command].failure, Counter, Total number of failed or cancelled requests for a specific Redis command
  upstream_commands.[command].total, Counter, Total number of requests for a specific Redis command (sum of success and failure)
  upstream_commands.[command].latency, Histogram, Latency of requests for a specific Redis command

Transactions
------------

Transactions (MULTI) are supported. Their use is no different from regular Redis: you start a transaction with MULTI,
and you execute it with EXEC. Within the transaction only commands that are supported by Envoy (see below) and are single-key
commands are supported, i.e. MGET and MSET are not supported. The DISCARD command is supported.

When working in Redis Cluster mode, Envoy will relay all the commands in the transaction to the node handling the first
key-based command in the transaction. It is the user's responsibility to ensure that all keys in the transaction are mapped
to the same hashslot, as commands will not be redirected.

Supported commands
------------------

At the protocol level, pipelines are supported.
Use pipelining wherever possible for the best performance.

At the command level, Envoy only supports commands that can be reliably hashed to a server. AUTH and PING
are the only exceptions. AUTH is processed locally by Envoy if a downstream password has been configured,
and no other commands will be processed until authentication is successful when a password has been
configured. Envoy will transparently issue AUTH commands upon connecting to upstream servers, if upstream
authentication passwords are configured for the cluster. Envoy responds to PING immediately with PONG.
Arguments to PING are not allowed. All other supported commands must contain a key. Supported commands are
functionally identical to the original Redis command except possibly in failure scenarios.

For details on each command's usage see the official
`Redis command reference <https://redis.io/commands>`_.

.. csv-table::
  :header: Command, Group
  :widths: 1, 1

  AUTH, Authentication
  PING, Connection
  QUIT, Connection
  DEL, Generic
  DISCARD, Transaction
  DUMP, Generic
  EXEC, Transaction
  EXISTS, Generic
  EXPIRE, Generic
  EXPIREAT, Generic
  PERSIST, Generic
  PEXPIRE, Generic
  PEXPIREAT, Generic
  PTTL, Generic
  RESTORE, Generic
  TOUCH, Generic
  TTL, Generic
  TYPE, Generic
  UNLINK, Generic
  GEOADD, Geo
  GEODIST, Geo
  GEOHASH, Geo
  GEOPOS, Geo
  GEORADIUS_RO, Geo
  GEORADIUSBYMEMBER_RO, Geo
  HDEL, Hash
  HEXISTS, Hash
  HGET, Hash
  HGETALL, Hash
  HINCRBY, Hash
  HINCRBYFLOAT, Hash
  HKEYS, Hash
  HLEN, Hash
  HMGET, Hash
  HMSET, Hash
  HSCAN, Hash
  HSET, Hash
  HSETNX, Hash
  HSTRLEN, Hash
  HVALS, Hash
  PFADD, HyperLogLog
  PFCOUNT, HyperLogLog
  LINDEX, List
  LINSERT, List
  LLEN, List
  LPOP, List
  LPUSH, List
  LPUSHX, List
  LRANGE, List
  LREM, List
  LSET, List
  LTRIM, List
  MULTI, Transaction
  RPOP, List
  RPUSH, List
  RPUSHX, List
  EVAL, Scripting
  EVALSHA, Scripting
  SADD, Set
  SCARD, Set
  SISMEMBER, Set
  SMEMBERS, Set
  SPOP, Set
  SRANDMEMBER, Set
  SREM, Set
  SSCAN, Set
  ZADD, Sorted Set
  ZCARD, Sorted Set
  ZCOUNT, Sorted Set
  ZINCRBY, Sorted Set
  ZLEXCOUNT, Sorted Set
  ZRANGE, Sorted Set
  ZRANGEBYLEX, Sorted Set
  ZRANGEBYSCORE, Sorted Set
  ZRANK, Sorted Set
  ZREM, Sorted Set
  ZREMRANGEBYLEX, Sorted Set
  ZREMRANGEBYRANK, Sorted Set
  ZREMRANGEBYSCORE, Sorted Set
  ZREVRANGE, Sorted Set
  ZREVRANGEBYLEX, Sorted Set
  ZREVRANGEBYSCORE, Sorted Set
  ZREVRANK, Sorted Set
  ZPOPMIN, Sorted Set
  ZPOPMAX, Sorted Set
  ZSCAN, Sorted Set
  ZSCORE, Sorted Set
  APPEND, String
  BITCOUNT, String
  BITFIELD, String
  BITPOS, String
  DECR, String
  DECRBY, String
  GET, String
  GETBIT, String
  GETRANGE, String
  GETSET, String
  INCR, String
  INCRBY, String
  INCRBYFLOAT, String
  MGET, String
  MSET, String
  PSETEX, String
  SET, String
  SETBIT, String
  SETEX, String
  SETNX, String
  SETRANGE, String
  STRLEN, String

Failure modes
-------------

If Redis throws an error, we pass that error along as the response to the command. Envoy treats a
response from Redis with the error datatype as a normal response and passes it through to the
caller.

Envoy can also generate its own errors in response to the client.

.. csv-table::
  :header: Error, Meaning
  :widths: 1, 1

  no upstream host, "The ring hash load balancer did not have a healthy host available at the
  ring position chosen for the key."
  upstream failure, "The backend did not respond within the timeout period or closed
  the connection."
  invalid request, "Command was rejected by the first stage of the command splitter due to
  datatype or length."
  unsupported command, "The command was not recognized by Envoy and therefore cannot be serviced
  because it cannot be hashed to a backend server."
  finished with n errors, "Fragmented commands which sum the response (e.g. DEL) will return the
  total number of errors received if any were received."
  upstream protocol error, "A fragmented command received an unexpected datatype or a backend
  responded with a response that not conform to the Redis protocol."
  wrong number of arguments for command, "Certain commands check in Envoy that the number of
  arguments is correct."
  "NOAUTH Authentication required.", "The command was rejected because a downstream authentication
  password has been set and the client has not successfully authenticated."
  ERR invalid password, "The authentication command failed due to an invalid password."
  "ERR Client sent AUTH, but no password is set", "An authentication command was received, but no
  downstream authentication password has been configured."


In the case of MGET, each individual key that cannot be fetched will generate an error response.
For example, if we fetch five keys and two of the keys' backends time out, we would get an error
response for each in place of the value.

.. code-block:: none

  $ redis-cli MGET a b c d e
  1) "alpha"
  2) "bravo"
  3) (error) upstream failure
  4) (error) upstream failure
  5) "echo"
