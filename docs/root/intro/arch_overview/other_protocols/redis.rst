.. _arch_overview_redis:

Redis
=======

Envoy can act as a Redis proxy, partitioning commands among instances in a cluster.
In this mode, the goals of Envoy are to maintain availability and partition tolerance
over consistency. This is the key point when comparing Envoy to `Redis Cluster
<https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/>`_. Envoy is designed as a best-effort cache,
meaning that it will not try to reconcile inconsistent data or keep a globally consistent
view of cluster membership. It also supports routing commands from different workloads to
different upstream clusters based on their access patterns, eviction, or isolation
requirements.

The Redis project offers a thorough reference on partitioning as it relates to Redis. See
"`Partitioning: how to split data among multiple Redis instances
<https://redis.io/docs/latest/operate/oss_and_stack/management/scaling/>`_".

**Features of Envoy Redis**:

* `Redis protocol <https://redis.io/docs/latest/develop/reference/protocol-spec/>`_ codec.
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

Envoy offers support for `Redis Cluster <https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/>`_.

When using Envoy as a sidecar proxy for a Redis Cluster, the service can use a non-cluster Redis client
implemented in any language to connect to the proxy as if it's a single node Redis instance.
The Envoy proxy will keep track of the cluster topology and send commands to the correct Redis node in the
cluster according to the `spec <https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/>`_. Advance features such as reading
from replicas can also be added to the Envoy proxy instead of updating Redis clients in each language.

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
  upstream_resp3_hello_failure, Counter, "Total number of upstream ``HELLO 3`` negotiations that did not result in a successful RESP3 handshake (error reply, wrong reply shape, connection error, or non-3 ``proto`` field). Incremented only when the listener's ``protocol_version`` is ``RESP3``."
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
and you execute it with EXEC. Within the transaction, from the list of commands supported by Envoy (see below), only single-key
commands (e.g. GET, SET), multi-key commands (e.g. DEL, MSET) and transaction commands (e.g. WATCH, UNWATCH, DISCARD, EXEC) are supported.
Commands configured via :ref:`custom_commands
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.custom_commands>` are also supported within
transactions and are treated as single-key commands.


When working in Redis Cluster mode, Envoy will relay all the commands in the transaction to the node handling the first
key-based command in the transaction. If this command is multi-key, it will send it to the server corresponding to the first key
in the command. It is the user's responsibility to ensure that all keys in the transaction are mapped to the same hashslot, as
commands will not be redirected.

Supported commands
------------------

At the protocol level, pipelines are supported.
Use pipelining wherever possible for the best performance.

At the command level, Envoy only supports commands that can be reliably hashed to a server. AUTH, PING, ECHO and INFO
are exceptions, as are HELLO, QUIT, and the CLIENT SETNAME and CLIENT SETINFO subcommands that Envoy handles
locally. AUTH is processed locally by Envoy if a downstream password has been configured,
and no other commands will be processed until authentication is successful when a password has been
configured. If an external authentication provider is set, Envoy will instead send the authentication arguments
to an external service and act according to the authentication response. If a downstream password is set together
with external authentication, the validation will be done still externally and the downstream password used for
upstream authentication. Envoy will transparently issue AUTH commands upon connecting to upstream servers,
if upstream authentication passwords are configured for the cluster. Envoy responds to PING immediately with PONG.
Arguments to PING are not allowed. Envoy responds to ECHO immediately with the command argument.
All other supported commands must contain a key. Supported commands are functionally identical to the
original Redis command except possibly in failure scenarios.

RESP Protocol
^^^^^^^^^^^^^
Envoy Redis proxy supports RESP2 and RESP3, selected by the listener's
:ref:`RedisProxy.protocol_version
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.protocol_version>`.
This single knob governs both downstream connections and every routed upstream conn pool —
there is no separate per-cluster RESP knob and no implicit floor across clusters.

The listener pins one RESP version end-to-end rather than translating between them, because
RESP2 and RESP3 diverge structurally: some replies have no transparent mapping (for example
``ZRANGE WITHSCORES`` is a flat array under RESP2 but a nested array of pairs under RESP3).
The proxy therefore does not cross-encode upstream responses — a reply is always emitted
downstream in the RESP version it arrived in. The exception is cluster-scope aggregate commands such as
``CONFIG GET`` and ``KEYS``, whose per-shard replies are combined into one flat array even when a shard
answers with a RESP3 Map. RESP3 ``Push`` frames from upstream are routed to the connection's
downstream subscriber when one is registered via ``SUBSCRIBE``
(see Pub/Sub Support below); on a connection without a registered subscriber a
Push is unexpected and is dropped with a debug log (the proxy opts into no other
Push-producing feature — ``CLIENT TRACKING`` is out of scope — and forwarding an unrouted
Push would desynchronize the reply FIFO). A non-Push reply arriving with no pending request is
handled by connection role. On a *subscription* connection it is Redis's error reply to a
fire-and-forget ``SSUBSCRIBE`` / ``SUNSUBSCRIBE`` control command: the connection is kept open
(closing it would re-issue every channel on it and drop its healthy subscriptions) and the error is
correlated — in send order, per host — to the oldest outstanding control command, failing that
channel's pending subscribers or, for a redirect (``-MOVED`` / ``-ASK`` / ``-CLUSTERDOWN``),
refreshing the slot map and re-resolving on a backoff. On an ordinary data connection an
unsolicited non-Push reply is treated as a protocol error and the connection is closed.

For the full ``HELLO 3`` negotiation, ``-NOPROTO`` gating of pre-handshake data commands,
downstream ``HELLO N AUTH`` handling, the locally-synthesized ``HELLO`` reply, and the
``upstream_resp3_hello_failure`` stat, see the :ref:`RESP protocol version
<config_network_filters_redis_proxy_protocol_version>` section of the Redis proxy filter
configuration.

Pub/Sub Support
^^^^^^^^^^^^^^^
Envoy's Redis proxy exposes the client-facing pub/sub commands ``SUBSCRIBE``,
``UNSUBSCRIBE``, ``PUBLISH``, and ``SPUBLISH``. The subscription commands
(``SUBSCRIBE`` / ``UNSUBSCRIBE``) require the downstream client to have negotiated
RESP3 with the proxy via ``HELLO 3`` because messages are delivered as RESP3 ``Push``
frames. Attempting them without RESP3 returns an error whose text depends on the
listener: on a listener configured for RESP3 the client simply has not upgraded yet, so it
is told ``ERR pub/sub requires RESP3. Send HELLO 3 first.``; on a listener that is not
configured for RESP3 — where ``HELLO 3`` is itself rejected with ``-NOPROTO``, making that
advice unachievable — it returns ``ERR pub/sub is not enabled on this listener (RESP3
required)`` instead.
``PUBLISH`` and ``SPUBLISH`` reply with an ordinary integer and have no RESP3
requirement — they work on RESP2 and RESP3 downstream connections alike.

Pattern subscriptions (``PSUBSCRIBE`` / ``PUNSUBSCRIBE``) are intentionally not
supported and are rejected with ``ERR unknown command``. The proxy delivers messages
through an exact-channel subscription registry with no pattern-matching layer, and it
rewrites every client ``SUBSCRIBE`` onto sharded ``SSUBSCRIBE`` (see "Transparent sharded
routing" below) — sharded pub/sub matches only exact channels, never classic patterns. A
pattern subscriber could therefore never receive a proxy-delivered message, so rather than
accept a subscription that would silently never fire, the proxy rejects it.

The sharded variants ``SSUBSCRIBE`` and ``SUNSUBSCRIBE`` are likewise not exposed —
see "Transparent sharded routing" below.

``PUBSUB`` introspection (``PUBSUB CHANNELS`` / ``NUMSUB`` / ``NUMPAT``, and the sharded
``PUBSUB SHARDCHANNELS`` / ``SHARDNUMSUB``) is not supported and is rejected with
``ERR unknown command``. Because the proxy transparently rewrites client ``SUBSCRIBE`` onto sharded
``SSUBSCRIBE`` (see "Transparent sharded routing" below), a classic ``PUBSUB`` query would not
reflect the real subscriptions, and the sharded query sent upstream would count the proxy's own
upstream subscription connections rather than downstream subscribers — misleading either way.
``RESET`` is likewise not supported and is rejected; a subscriber's state is cleaned up when its
downstream connection closes, not via ``RESET``. Forcing any of these commands through with
``custom_commands`` bypasses the proxy's pub/sub bookkeeping and yields results inconsistent with
the proxy's own subscription state.

Pub/sub messages are delivered as RESP3 Push frames on shared upstream connections, enabling
efficient fan-out: multiple downstream subscribers to the same channel share a single upstream
subscription. The proxy maintains a per-thread subscription registry that manages
reference-counted upstream subscriptions — the first subscriber triggers an upstream
subscribe, and the last unsubscribe triggers an upstream unsubscribe.

A subscriber that cannot keep up with its channels' message rate is a slow consumer: unlike a
request/response client, which self-paces by withholding requests, it has no flow control over the
unsolicited Push frames sent to it. To bound memory the proxy closes such a subscriber's downstream
connection once its write buffer exceeds the connection's high watermark — the existing
:ref:`per_connection_buffer_limit_bytes
<envoy_v3_api_field_config.listener.v3.Listener.per_connection_buffer_limit_bytes>` limit, reused
here rather than a pub/sub-specific option — mirroring Redis's ``client-output-buffer-limit`` pubsub
eviction. Each eviction increments the ``pubsub_slow_subscriber_closed`` counter.

The subscription registry is tied to the upstream cluster's lifecycle. When the cluster is removed
or **updated** (a cluster update is processed as a removal followed by an add — the common case is a
routine CDS refresh, e.g. an endpoint or health-check change), the proxy tears the registry down and
closes every subscribed downstream connection. Unlike a request/response client — whose in-flight
requests transparently re-route to the refreshed cluster — a pub/sub subscriber must therefore
reconnect and re-issue its ``SUBSCRIBE`` after such an event. Deployments that update the pub/sub
cluster frequently should expect subscribers to reconnect on each update.

Transparent sharded routing
"""""""""""""""""""""""""""
The proxy transparently rewrites client pub/sub commands onto Redis Cluster's
sharded pub/sub protocol so that messages route to the slot-owning shard rather
than broadcasting across the cluster:

* ``SUBSCRIBE`` channel → upstream ``SSUBSCRIBE`` per channel, with the channel
  hashed via CRC16 to the owning shard.
* ``UNSUBSCRIBE`` channel → upstream ``SUNSUBSCRIBE`` against the same shard.
* ``PUBLISH`` channel value → upstream ``SPUBLISH`` when
  :ref:`enable_sharded_publish
  <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.enable_sharded_publish>`
  is set (default off); otherwise the classic ``PUBLISH`` is forwarded unchanged.
* ``SPUBLISH`` channel value → upstream ``SPUBLISH`` regardless of ``enable_sharded_publish``. An
  explicit client ``SPUBLISH`` is a shard command, so its channel is always routed as a sharded
  pub/sub channel; the ``enable_sharded_publish`` flag governs only whether a plain ``PUBLISH`` is
  rewritten, never an already-sharded ``SPUBLISH``.

Because the rewrite is transparent, downstream ack and message frames continue
to use the client-facing verbs: a ``SUBSCRIBE`` is acknowledged as
``["subscribe", channel, count]``, sharded messages arrive as
``["message", ...]`` (not ``smessage``), and clients never observe ``ssubscribe``.

Sharded pub/sub requires Redis 7.0 or newer on the upstream cluster. The ``SUBSCRIBE`` /
``UNSUBSCRIBE`` rewrite is applied unconditionally on a ``RESP3`` listener; the ``PUBLISH``
→ ``SPUBLISH`` rewrite is opt-in via :ref:`enable_sharded_publish
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.enable_sharded_publish>`
(default off). Because ``SUBSCRIBE`` is always sharded, a ``PUBLISH`` left classic (the
default) is not delivered to subscribers that subscribed through this proxy — enable the
option whenever the listener serves sharded pub/sub subscribers, and only against Redis 7.0+
upstreams (older upstreams have no ``SPUBLISH`` and every rewritten publish would fail).

To enable pub/sub, set the listener's :ref:`RedisProxy.protocol_version
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.protocol_version>`
to ``RESP3``. Both sides of the proxy then speak RESP3 — upstream (so ``HELLO 3`` is sent on
each connection) and downstream, where the client must have completed its own ``HELLO 3``
handshake (so a Push frame is acceptable on the wire) before a ``SUBSCRIBE`` is accepted;
otherwise the proxy returns ``ERR pub/sub requires RESP3``.

If a ``SUBSCRIBE`` channel cannot be routed (no upstream cluster matches the route, or the
listener is not configured for RESP3), the proxy delivers an inline error frame for that channel
on the subscriber's connection rather than a fake success ack — the subscriber sees the
failure clearly, and other channels in the same multi-channel ``SUBSCRIBE`` still proceed.

A ``SUBSCRIBE`` that is routed and sent upstream but never acknowledged — for example against an
upstream that keeps rejecting the ``SSUBSCRIBE`` (a pre-7.0 Redis with no sharded pub/sub, or an
ACL/CROSSSLOT denial) — does not stay pending forever. If the upstream rejects the ``SSUBSCRIBE``
outright, or no ack arrives within an internal subscribe-ack timeout, the proxy rolls back the
optimistic subscription (including its active-subscription gauge contribution) and closes the
subscriber's connection. The original ``SUBSCRIBE`` request has already completed — its confirmation
is delivered out of band, like every pub/sub frame — so there is no in-band reply left to carry an
error, and writing an unsolicited ``-ERR`` out of band would desync a pipelining RESP3 client (which
would misattribute the error to an earlier, still-in-flight command). Closing the connection is the
protocol-clean signal instead: the client observes a clear failure rather than a silently
unacknowledged ``SUBSCRIBE`` and reconnects to retry.

The active-subscription gauge counts per-downstream-subscriber subscriptions (not per-thread
distinct channels), so two downstream subscribers to the same channel correctly increment the
gauge twice — and a disconnect of one decrements only that subscriber's contribution while the
other remains active.

.. note::

  Subscribe ack timing depends on whether an upstream subscribe is actually issued:

  * **Channel was new on this thread** — the registry sends ``SSUBSCRIBE`` upstream
    (the rewritten ``SUBSCRIBE`` path) and parks the
    downstream subscriber. The
    downstream subscribe ack is delivered when the matching upstream Push subscribe-ack
    arrives, so the client only sees its ack after the upstream has confirmed. The ack
    carries the subscriber's per-subscriber count snapshotted at subscribe-call time
    (matching Redis's "number of subscriptions the client now has at THIS step" semantic).
  * **Channel is already fully subscribed by another downstream subscriber on this thread** — the
    registry deduplicates and does not send a fresh upstream subscribe, and the earlier upstream ack
    has already arrived, so none will fire on this subscriber's behalf. The proxy fabricates the ack
    immediately to match Redis's per-client semantics.
  * **Channel is still subscribing (an earlier subscriber's upstream ack has not arrived yet)** — the
    new subscriber joins the pending bucket rather than receiving a fabricated ack, so it is
    confirmed together with the others when the shared upstream ack lands — or fails together with
    them on the subscribe-ack timeout. This avoids handing back a premature success the upstream
    might still reject.

  The pre-dispatch failure paths (no route, non-RESP3 listener, conn-pool send failure)
  surface as inline errors per the section above. An upstream-side error that arrives
  *after* the upstream subscribe was sent (a Redis ``-ERR`` reply to the ``SUBSCRIBE``
  itself, or an upstream connection close before the ack returns) does not propagate as a
  per-channel error to the downstream client; it surfaces through the upstream connection's
  close path, which triggers the registry's resubscribe handler — a fresh subscribe is
  reissued and the eventual ack drains the still-pending entry.

  All subscribe and unsubscribe acks are RESP3 ``Push`` frames delivered out-of-band over the
  subscriber connection, never through the in-band response FIFO — so a ``SUBSCRIBE`` /
  ``UNSUBSCRIBE`` ack may arrive ahead of an earlier pipelined command's reply. Within one
  multi-channel ``SUBSCRIBE`` the per-channel acks are emitted as each channel's owning shard
  confirms (deduplicated channels immediately), so their order is not guaranteed to match the
  order the channels appeared in the command. Every ack carries its channel name, so clients must
  match acks by the channel field rather than by position — the standard way RESP3 pub/sub clients
  consume ``Push`` frames, and an inherent property of fanning one command across independent
  shards rather than a single ordered connection.

.. note::

  **``PUBLISH`` inside ``MULTI``.** A ``PUBLISH`` issued inside a ``MULTI`` / ``EXEC`` transaction is
  not rewritten to ``SPUBLISH`` even when :ref:`enable_sharded_publish
  <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.enable_sharded_publish>`
  is set. A transaction is pinned to a single shard, which is generally not the channel's slot
  owner, so a sharded rewrite would route the publish to the wrong shard; it is instead forwarded
  unchanged as part of the transaction and is therefore not delivered to sharded ``SSUBSCRIBE``
  subscribers of that channel through this proxy. Issue such a ``PUBLISH`` outside a transaction.

  **Slow-subscriber backpressure.** A subscriber that consumes slower than messages are published
  is bounded: once such a connection's write buffer exceeds its high watermark, the proxy closes the
  subscriber's downstream connection (see "Pub/sub" above), reusing the connection's
  :ref:`per_connection_buffer_limit_bytes
  <envoy_v3_api_field_config.listener.v3.Listener.per_connection_buffer_limit_bytes>` limit rather
  than a pub/sub-specific option, and increments ``pubsub_slow_subscriber_closed``. This mirrors
  Redis's ``client-output-buffer-limit pubsub`` eviction. The bound applies to a connection that is
  subscribed or is holding parked (ordering-pending) push frames; a connection with no active
  subscription is unaffected.

  **Cluster update.** A management-server update to the upstream cluster this listener routes to is
  applied as a remove-then-add of the cluster, which rebuilds the thread-local subscription state;
  downstream connections currently subscribed through that cluster are closed and must reconnect and
  re-``SUBSCRIBE``. Ordinary (non-subscription) data connections are unaffected. This applies to any
  routine control-plane update to that cluster, not only its removal.

INFO command
^^^^^^^^^^^^
INFO command is handled by Envoy differently it aggregates metrics across all shards and returns consolidated cluster-wide statistics.
An optional section parameter can be provided to filter the output (e.g., INFO memory).
INFO.SHARD is an Envoy-specific command introduced for debugging purposes that queries a specific shard by index
and returns that shard's complete INFO response (e.g., INFO.SHARD 0 memory).
Shard numbering starts from 0 and shards are ordered from lowest to highest slot assignment.
when using INFO.SHARD command, if the provided shard index is invalid, Envoy will return an error.
when using INFO.SHARD command, via redis-cli, make sure to use --raw flag to get the proper output format.

For details on each command's usage see the official
`Redis command reference <https://redis.io/commands>`_.

.. csv-table::
  :header: Command, Group
  :widths: 1, 1

  AUTH, Authentication
  CLIENT, Connection
  ECHO, Connection
  HELLO, Connection
  PING, Connection
  QUIT, Connection
  DEL, Generic
  DISCARD, Transaction
  DUMP, Generic
  EXEC, Transaction
  EXISTS, Generic
  EXPIRE, Generic
  EXPIREAT, Generic
  KEYS, String
  PERSIST, Generic
  PEXPIRE, Generic
  PEXPIREAT, Generic
  PTTL, Generic
  RESTORE, Generic
  SELECT, Generic
  TOUCH, Generic
  TTL, Generic
  TYPE, Generic
  UNLINK, Generic
  COPY, Generic
  RENAME, Generic
  RENAMENX, Generic
  SORT, Generic
  SORT_RO, Generic
  SCRIPT, Generic
  FLUSHALL, Generic
  FLUSHDB, Generic
  SLOWLOG, Generic
  CONFIG, Generic
  CLUSTER INFO, Generic
  CLUSTER SLOTS, Generic
  CLUSTER KEYSLOT, Generic
  CLUSTER NODES, Generic
  RANDOMKEY, Generic
  OBJECT, Generic
  GEOADD, Geo
  GEODIST, Geo
  GEOHASH, Geo
  GEOPOS, Geo
  GEORADIUS_RO, Geo
  GEORADIUSBYMEMBER_RO, Geo
  GEOSEARCH, Geo
  GEOSEARCHSTORE, Geospatial
  GEORADIUS, Geospatial
  GEORADIUSBYMEMBER, Geospatial
  HDEL, Hash
  HEXISTS, Hash
  HEXPIRE, Hash
  HEXPIREAT, Hash
  HEXPIRETIME, Hash
  HGET, Hash
  HGETALL, Hash
  HINCRBY, Hash
  HINCRBYFLOAT, Hash
  HKEYS, Hash
  HLEN, Hash
  HMGET, Hash
  HMSET, Hash
  HPERSIST, Hash
  HPEXPIRE, Hash
  HPEXPIREAT, Hash
  HPEXPIRETIME, Hash
  HPTTL, Hash
  HRANDFIELD, Hash
  HSCAN, Hash
  HSET, Hash
  HSETNX, Hash
  HSTRLEN, Hash
  HTTL, Hash
  HVALS, Hash
  PFADD, HyperLogLog
  PFCOUNT, HyperLogLog
  PFMERGE, HyperLogLog
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
  LPOS, List
  RPOPLPUSH, List
  MULTI, Transaction
  RPOP, List
  RPUSH, List
  RPUSHX, List
  PUBLISH, Pubsub
  SPUBLISH, Pubsub
  SUBSCRIBE, Pubsub
  UNSUBSCRIBE, Pubsub
  EVAL, Scripting
  EVALSHA, Scripting
  SADD, Set
  SCARD, Set
  SISMEMBER, Set
  SMEMBERS, Set
  SPOP, Set
  SRANDMEMBER, Set
  SREM, Set
  SCAN, Generic
  SSCAN, Set
  SDIFF, Set
  SDIFFSTORE, Set
  SINTER, Set
  SINTERSTORE, Set
  SMISMEMBER, Set
  SMOVE, Set
  SUNION, Set
  SUNIONSTORE, Set
  WATCH, String
  UNWATCH, String
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
  ZDIFF, Sorted Set
  ZDIFFSTORE, Sorted Set
  ZINTER, Sorted Set
  ZINTERSTORE, Sorted Set
  ZMSCORE, Sorted Set
  ZRANDMEMBER, Sorted Set
  ZRANGESTORE, Sorted Set
  ZUNION, Sorted Set
  ZUNIONSTORE, Sorted Set
  APPEND, String
  BITCOUNT, String
  BITFIELD, String
  BITFIELD_RO, String
  BITPOS, String
  DECR, String
  DECRBY, String
  GET, String
  GETBIT, String
  GETDEL, String
  GETEX, String
  GETRANGE, String
  GETSET, String
  INCR, String
  INCRBY, String
  INCRBYFLOAT, String
  INFO, Server
  INFO.SHARD, Server
  ROLE, Server
  MGET, String
  MSET, String
  PSETEX, String
  SET, String
  SETBIT, String
  SETEX, String
  SETNX, String
  SETRANGE, String
  STRLEN, String
  MSETNX, String
  SUBSTR, String
  XACK, Stream
  XADD, Stream
  XAUTOCLAIM, Stream
  XCLAIM, Stream
  XDEL, Stream
  XLEN, Stream
  XPENDING, Stream
  XRANGE, Stream
  XREVRANGE, Stream
  XTRIM, Stream
  BF.ADD, Bloom
  BF.CARD, Bloom
  BF.EXISTS, Bloom
  BF.INFO, Bloom
  BF.INSERT, Bloom
  BF.LOADCHUNK, Bloom
  BF.MADD, Bloom
  BF.MEXISTS, Bloom
  BF.RESERVE, Bloom
  BF.SCANDUMP, Bloom
  BITOP, Bitmap

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
  ERR unknown command, "The command was not recognized by Envoy and therefore cannot be serviced
  because it cannot be hashed to a backend server."
  finished with n errors, "Fragmented commands which sum the response (e.g. DEL) will return the
  total number of errors received if any were received."
  upstream protocol error, "A fragmented command received an unexpected datatype or a backend
  responded with a response that not conform to the Redis protocol."
  wrong number of arguments for command, "Certain commands check in Envoy that the number of
  arguments is correct."
  "NOAUTH Authentication required.", "The command was rejected because a downstream authentication
  password or external authentication have been set and the client has not successfully authenticated."
  ERR invalid password, "The authentication command failed due to an invalid password."
  ERR <external-message>, "The authentication command failed on the external auth provider."
  "ERR Client sent AUTH, but no password is set", "An authentication command was received, but no
  downstream authentication password or external authentication provider have been configured."
  ERR invalid cursor, "The iteration command failed due to an invalid or unrecognized cursor."


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

Protocol
--------

Although `RESP <https://redis.io/docs/latest/develop/reference/protocol-spec/>`_ is recommended for production use,
`inline commands <https://redis.io/docs/latest/develop/reference/protocol-spec/#inline-commands>`_ are also supported.
