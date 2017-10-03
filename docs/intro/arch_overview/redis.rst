.. _arch_overview_redis:

Redis
=======

Envoy can act as a Redis proxy, partitioning commands among instances in a cluster.
In this mode, the goals of Envoy are to maintain availability and partition tolerance
over consistency. This is the key point when comparing Envoy to `Redis Cluster
<https://redis.io/topics/cluster-spec>`_. Envoy is designed as a best-effort cache,
meaning that it will not try to reconcile inconsistent data or keep a globally consistent
view of cluster membership.

The Redis project offers a thorough reference on partitioning as it relates to Redis. See
"`Partitioning: how to split data among multiple Redis instances
<https://redis.io/topics/partitioning>`_".

**Features of Envoy Redis**:

* `Redis protocol <https://redis.io/topics/protocol>`_ codec.
* Hash-based partitioning.
* Ketama distribution.
* Detailed command statistics.
* Active and passive healthchecking.

**Planned future enhancements**:

* Additional timing stats.
* Circuit breaking.
* Request collapsing for fragmented commands.
* Replication.
* Built-in retry.
* Tracing.
* Hash tagging.

.. _arch_overview_redis_configuration:

Configuration
-------------

For filter configuration details, see the Redis proxy filter
:ref:`configuration reference <config_network_filters_redis_proxy>`.

The corresponding cluster definition should be configured with
:ref:`ring hash load balancing <config_cluster_manager_cluster_lb_type>`.

If active healthchecking is desired, the cluster should be configured with a
:ref:`Redis healthcheck <config_cluster_manager_cluster_hc>`.

If passive healthchecking is desired, also configure
:ref:`outlier detection <config_cluster_manager_cluster_outlier_detection_summary>`.

For the purposes of passive healthchecking, connect timeouts, command timeouts, and connection
close map to 5xx. All other responses from Redis are counted as a success.

Supported commands
------------------

At the protocol level, pipelines are supported. MULTI (transaction block) is not.
Use pipelining wherever possible for the best performance.

At the command level, Envoy only supports commands that can be reliably hashed to a server.
Therefore, all supported commands contain a key. Supported commands are functionally identical to
the original Redis command except possibly in failure scenarios.

For details on each command's usage see the official
`Redis command reference <https://redis.io/commands>`_.

.. csv-table::
  :header: Command, Group
  :widths: 1, 1

  DEL, Generic
  DUMP, Generic
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
