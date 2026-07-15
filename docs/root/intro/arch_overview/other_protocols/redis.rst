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
Envoy's Redis proxy exposes the client-facing pub/sub commands
``SUBSCRIBE``, ``UNSUBSCRIBE``, ``PUBLISH``, and ``SPUBLISH``. The subscription commands
(``SUBSCRIBE`` / ``UNSUBSCRIBE``) require the downstream client to have negotiated RESP3 with the
proxy via ``HELLO 3`` because messages are delivered as RESP3 ``Push`` frames. Attempting them
without RESP3 returns an error whose text depends on the listener. On a listener configured for
RESP3, a ``SUBSCRIBE`` sent before the client completes ``HELLO 3`` is rejected by the pre-HELLO
gate described above — the same ``NOPROTO unsupported protocol version`` reply any pre-handshake
data command receives — so the client completes ``HELLO 3`` and retries. On a listener that is *not*
configured for RESP3, where ``HELLO 3`` is itself rejected with ``-NOPROTO`` (so upgrading is
impossible), a ``SUBSCRIBE`` instead returns
``ERR pub/sub is not enabled on this listener (RESP3 required)``. ``PUBLISH`` and ``SPUBLISH`` reply
with an ordinary integer and have no RESP3 requirement — they work on RESP2 and RESP3 downstream
connections alike. That integer is the receiver count the *upstream* returns (how many upstream
subscription connections were subscribed to the channel), not the number of downstream subscribers
reached through this proxy: because many downstream subscribers of a channel share a single upstream
subscription, the two counts differ.

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

A subscribed RESP3 client may continue issuing ordinary commands. Every reply the proxy sends that
client — an ordinary command reply, a ``subscribe`` / ``unsubscribe`` ack, and an inline pub/sub
``-ERR`` — flows through a single in-band response FIFO, ordered at the position of the command that
produced it, so no reply overtakes an earlier one. ``message`` frames are ordered against those
replies too: a message never overtakes the reply of a command the client issued before it (so a
client's own ``PUBLISH`` reply always precedes the ``message`` that publish produced). The one
asymmetry is that a ``SUBSCRIBE`` establishing *new* upstream subscriptions holds its FIFO slot
until the upstream confirms (see the ordering note below), so a command pipelined behind it waits
for that confirmation just as it would behind any other in-flight command.

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
closes every subscribed downstream connection. A request/response client keeps its downstream
connection across such an update, but its in-flight commands are failed with an error and must be
retried by the client — they are not transparently re-routed to the refreshed cluster. A pub/sub
subscriber goes further: its connection is closed outright, so it must reconnect and re-issue its
``SUBSCRIBE`` after such an event. Deployments that update the pub/sub cluster frequently should
expect subscribers to reconnect on each update.

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

Within the slot-owning shard, each channel's ``SSUBSCRIBE`` is homed by the conn pool's
:ref:`read_policy
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.read_policy>`
— subscriptions follow the same routing rule as reads. Under ``MASTER`` (the default) every channel
homes on the shard's primary, matching the data path so a channel's subscription and its
``SPUBLISH`` share one node. Under the replica-capable policies (``REPLICA``, ``PREFER_REPLICA``,
``ANY``) fresh placements land on whichever member the read rule selects, offloading the primary's
cluster-bus fan-out egress: each shard node pays one cluster-bus frame per published message
regardless of how many subscribers it serves, so subscriptions homed on replicas cut the primary's
egress at no extra delivery cost. The zone-affinity policies prefer same-zone members with the same
fallback chain reads use. A replica-homed subscription receives each message over a fire-and-forget
cluster-bus hop — at-most-once, the same best-effort delivery the pub/sub contract already provides
(a message can be missed across a resharding window or on slow-subscriber eviction).

Placement is consulted only when a channel is first placed or must be re-placed; an established
subscription stays pinned to its recorded host. Under ``MASTER`` a moved primary re-homes the
channel (read/subscribe parity); under the replica-capable policies the subscription stays on its
member while that member remains in the shard, and if the member leaves the shard the proxy
re-places the channel onto a current member without disturbing the downstream subscriber.

A member that remains in the shard but becomes unreachable is re-placed onto a healthy sibling on
the next re-subscribe — but only after Envoy marks that host unhealthy. A subscription connection's
``SSUBSCRIBE`` is fire-and-forget, so it does not by itself drive a health signal; this escape
therefore depends on the member actually being marked unhealthy by upstream active health checking,
outlier detection, or (for a replica that also serves reads) data-path traffic. Configure active
health checking on a cluster whose ``read_policy`` homes subscriptions on replicas so a
persistently-unreachable member does not hold its channels indefinitely.

More generally — with any placement policy — an established subscription connection carries only
fire-and-forget ``SSUBSCRIBE`` / ``SUNSUBSCRIBE`` traffic, so it holds no in-flight request whose
timeout would notice a *silent* upstream failure: a path drop, NIC/VM freeze, or conntrack loss that
never delivers a TCP reset. Such a connection stays locally "open" and delivers no messages until
the OS default keepalive (typically hours) or a failed ``SSUBSCRIBE`` retransmission eventually
tears it down, and no re-subscribe fires in the meantime. Configure the cluster's
:ref:`upstream_connection_options.tcp_keepalive
<envoy_v3_api_field_config.cluster.v3.Cluster.upstream_connection_options>` so a silently-gone
member is detected in bounded time; the resulting connection close drives the re-subscribe onto a
healthy member.

Sharded pub/sub requires Redis 7.0 or newer on the upstream cluster. On a ``RESP3`` listener the
``SUBSCRIBE`` / ``UNSUBSCRIBE`` rewrite is the default (:ref:`sharded_subscription_mode
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.PubsubSettings.sharded_subscription_mode>`
``SHARDED``); setting it to ``DISABLED`` instead rejects ``SUBSCRIBE`` / ``UNSUBSCRIBE`` with
``ERR unknown command`` — for a RESP3 upstream that does not implement ``SSUBSCRIBE`` (e.g. Redis
6.x). The ``PUBLISH`` → ``SPUBLISH`` rewrite is opt-in via :ref:`enable_sharded_publish
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.enable_sharded_publish>`
(default off). Because a ``SHARDED``-mode ``SUBSCRIBE`` is sharded, a ``PUBLISH`` left classic (the
default) is not delivered to subscribers that subscribed through this proxy — enable the
option whenever the listener serves sharded pub/sub subscribers, and only against Redis 7.0+
upstreams (older upstreams have no ``SPUBLISH`` and every rewritten publish would fail). Enabling
the rewrite also write-classifies the upstream ``spublish`` verb, so a channel's publishes and its
rewritten sharded subscription pin to the same conn pool. That write-classification has a separate
consequence for request mirroring: under a :ref:`request_mirror_policy
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.PrefixRoutes.Route.request_mirror_policy>`
whose :ref:`exclude_read_commands
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.PrefixRoutes.Route.RequestMirrorPolicy.exclude_read_commands>`
is set, a rewritten publish is now classified as a write and is therefore mirrored (a classic
``PUBLISH`` was not), and a mirror running Redis < 7.0 rejects the mirrored ``SPUBLISH`` with
``-ERR unknown command``.

To enable pub/sub, set the listener's :ref:`RedisProxy.protocol_version
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.protocol_version>`
to ``RESP3``. Both sides of the proxy then speak RESP3 — upstream (so ``HELLO 3`` is sent on
each connection) and downstream, where the client must have completed its own ``HELLO 3``
handshake (so a Push frame is acceptable on the wire) before a ``SUBSCRIBE`` is accepted; a
``SUBSCRIBE`` sent before that handshake is rejected by the pre-HELLO ``-NOPROTO`` gate, as
described under *Pub/Sub Support* above.

If a ``SUBSCRIBE`` channel cannot be routed (no upstream cluster matches the channel's route), the
proxy delivers an inline error frame for that channel on the subscriber's connection rather than a
fake success ack — the subscriber sees the failure clearly, and other channels in the same
multi-channel ``SUBSCRIBE`` still proceed. A listener not configured for RESP3 is a separate,
command-level rejection: the whole ``SUBSCRIBE`` is refused with a single ``-ERR`` (pub/sub is
unavailable on a non-RESP3 listener) before any per-channel routing, not a per-channel error frame.

A ``SUBSCRIBE`` that is routed and sent upstream but never acknowledged — for example against an
upstream that keeps rejecting the ``SSUBSCRIBE`` (a pre-7.0 Redis with no sharded pub/sub, or an
ACL/CROSSSLOT denial) — does not stay pending forever. If the upstream rejects the ``SSUBSCRIBE``
outright, or no ack arrives within the operator-tunable
:ref:`subscribe-ack timeout <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.PubsubSettings.subscribe_ack_timeout>`,
the proxy rolls back the
optimistic subscription (including its active-subscription gauge contribution) and closes the
subscriber's connection. The original ``SUBSCRIBE`` request is still parked in the response FIFO
awaiting that upstream ack; the registry's completion path carries only the *success* ack, and the
failure is a shared-upstream-subscription failure that can strand several subscribers at once — so
rather than fabricate a per-request in-band ``-ERR`` (which, firing from an asynchronous timeout, a
pipelining RESP3 client could misattribute to a later in-flight command), the registry signals the
failure by closing each affected subscriber's connection. Closing is the protocol-clean signal: the
client observes a clear failure rather than a silently unacknowledged ``SUBSCRIBE`` and reconnects
to retry.

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
    arrives, so the client only sees its ack after the upstream has confirmed — unless the client
    ``UNSUBSCRIBE``s that channel before the ack lands, in which case the ack is completed
    immediately (see the *``UNSUBSCRIBE`` before the upstream confirms* note below). The ack
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
  surface as inline errors per the section above. An error that arrives *after* the upstream
  subscribe was sent is handled by its cause. A Redis ``-ERR`` reply to the ``SSUBSCRIBE`` itself
  (ACL, ``CROSSSLOT``, or an unknown command on a pre-7.0 upstream) fails that channel's
  still-pending downstream subscribers immediately — the subscription is rolled back, and for a
  fresh subscribe the subscriber connection is closed — rather than leaving them hanging until the
  subscribe-ack timeout; a channel that still has other active subscribers is instead re-resolved
  onto a healthy shard member. An upstream *connection close* before the ack returns (as distinct
  from an ``-ERR`` reply) triggers the registry's resubscribe handler on backoff, which re-issues
  the subscribe and drains the still-pending entry when the eventual ack lands.

  A slot migration that redirects the ``SSUBSCRIBE`` (a ``-MOVED`` / ``-ASK`` / ``-CLUSTERDOWN``
  reply) triggers a throttled cluster-topology refresh and a backoff re-resolve onto the channel's
  new shard. Note that the subscription path treats ``-ASK`` like ``-MOVED`` — a topology-refresh
  signal — rather than performing the data path's per-key ``ASKING`` handshake and retry to the
  migration target: sharded pub/sub signals slot moves with an unsolicited ``SUNSUBSCRIBE``, not a
  per-key ``-ASK``. A subscribe caught mid-migration is therefore re-resolved once the refresh
  lands, or, if the migration does not settle within the subscribe-ack timeout, closed so the
  client reconnects and retries.

  Every subscribe / unsubscribe ack is emitted as a response frame at its command's position in the
  in-band response FIFO, so it never overtakes an earlier pipelined command's reply. An ack the
  proxy can produce immediately — an ``UNSUBSCRIBE``, or a ``SUBSCRIBE`` for a channel already
  active on this thread — flushes as soon as that command reaches the front of the FIFO. A
  ``SUBSCRIBE`` that establishes one or more *new* upstream subscriptions is instead held in the
  FIFO until every one of its channels' ``SSUBSCRIBE`` acks has landed, then all of its acks flush
  together, in command-argument order, at its FIFO position — so a fresh subscribe's ack is ordered
  exactly where a single serial Redis connection would place it, and a command pipelined behind a
  slow subscribe waits for its reply just as it would behind any other in-flight command (a
  subscribe whose upstream never confirms is bounded by the subscribe-ack timeout above, which
  closes the connection). Every ack still carries its channel name, the field RESP3 pub/sub clients
  match on.

.. note::

  **``UNSUBSCRIBE`` before the upstream confirms.** A client may ``UNSUBSCRIBE`` a channel whose
  ``SUBSCRIBE`` is still parked in the FIFO awaiting its upstream ``SSUBSCRIBE`` ack. The proxy
  treats this as a client-cancelled subscribe: it completes the parked ``SUBSCRIBE`` immediately
  with its ``subscribe`` ack (the per-subscriber count snapshotted at subscribe-call time) at that
  request's FIFO slot, then the ``unsubscribe`` ack behind it — so the client sees the
  Redis-compatible ``subscribe ch 1`` / ``unsubscribe ch 0`` order without waiting for the upstream
  round-trip. The proxy still issues the upstream ``SUNSUBSCRIBE`` to release the
  (possibly-established) upstream subscription; any late ``SSUBSCRIBE`` ack or error for that
  now-cancelled channel is ignored, because the subscriber has already unsubscribed and is not
  waiting for messages on it. This is a deliberate trade-off — a prompt, correctly-ordered ack for a
  subscription the client has already abandoned, rather than holding the request for a strict
  upstream confirmation it no longer needs: it never closes the connection for such a cancel, and it
  cannot mis-deliver, because the channel is no longer subscribed.

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
  subscriber's downstream connection (see *Pub/Sub Support* above), reusing the connection's
  :ref:`per_connection_buffer_limit_bytes
  <envoy_v3_api_field_config.listener.v3.Listener.per_connection_buffer_limit_bytes>` limit rather
  than a pub/sub-specific option, and increments ``pubsub_slow_subscriber_closed``. This mirrors
  Redis's ``client-output-buffer-limit pubsub`` eviction. The bound applies to a connection that is
  subscribed or is holding parked (ordering-pending) push frames; a connection with no active
  subscription is unaffected.

  **Cluster update.** A management-server update to the upstream cluster this listener routes to is
  applied as a remove-then-add of the cluster, which rebuilds the thread-local subscription state;
  downstream connections currently subscribed through that cluster are closed and must reconnect and
  re-``SUBSCRIBE``. Ordinary (non-subscription) data connections survive the update, but any command
  in flight at that instant is failed with an error and must be retried by the client (it is not
  transparently re-routed). This applies to any routine control-plane update to that cluster, not
  only its removal.

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
