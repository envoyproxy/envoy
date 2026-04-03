Added pub/sub support to the Redis proxy on ``RESP3`` listeners: ``SUBSCRIBE``, ``UNSUBSCRIBE``,
``PUBLISH``, and ``SPUBLISH`` are now handled, with messages delivered as RESP3 Push frames over
shared upstream subscriptions (multiple downstream subscribers to one channel share a single
upstream subscription per worker thread). By default (``sharded_subscription_mode`` ``SHARDED``)
client ``SUBSCRIBE`` / ``UNSUBSCRIBE`` are transparently rewritten onto Redis Cluster sharded pub/sub
(upstream ``SSUBSCRIBE`` / ``SUNSUBSCRIBE``) so traffic routes to the slot-owning shard instead of
broadcasting; downstream ack and message frames keep the client-facing verbs, and ``SSUBSCRIBE`` /
``SUNSUBSCRIBE`` are not client-accessible. Client ``SPUBLISH`` is handled as an explicit sharded
publish — it shares the ``PUBLISH`` command handler but is always forwarded upstream as ``SPUBLISH``.
Client ``PUBLISH`` is rewritten to sharded
``SPUBLISH`` only when the new
:ref:`enable_sharded_publish <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.enable_sharded_publish>`
listener option is set (default off); otherwise it is forwarded unchanged as classic ``PUBLISH``,
so existing RESP2 / Redis < 7.0 deployments are unaffected. When the rewrite is enabled the upstream
``spublish`` verb is write-classified so a channel's publishes and its rewritten sharded subscription
pin to the same conn pool under a ``read_command_policy`` (classic ``PUBLISH`` keeps its prior
read/mirror routing). Pattern subscriptions (``PSUBSCRIBE`` / ``PUNSUBSCRIBE``) are intentionally not
supported and are rejected with ``ERR unknown command``. ``PUBLISH`` inside a ``MULTI`` transaction
routes through the transaction handler with the verb the client sent. Subscription commands require
the downstream client to have negotiated ``HELLO 3``; ``PUBLISH`` / ``SPUBLISH`` also work on RESP2
downstream connections. A slow subscriber whose downstream write buffer overruns the connection's
``per_connection_buffer_limit_bytes`` is closed to bound memory (mirroring Redis's pub/sub
``client-output-buffer-limit`` eviction), tracked by the new ``pubsub_slow_subscriber_closed``
counter. The subscribe-ack timeout and the sharded-subscription resubscribe backoff (base and max
interval) used by the failover path are tunable via the new
:ref:`pubsub_settings <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.pubsub_settings>`
connection-pool option; each field defaults to its previous hardcoded value, so existing
configurations are unaffected. For a RESP3-speaking upstream that does not implement ``SSUBSCRIBE``
(e.g. Redis 6.x), set
:ref:`pubsub_settings.sharded_subscription_mode <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.ConnPoolSettings.PubsubSettings.sharded_subscription_mode>`
to ``DISABLED`` to reject ``SUBSCRIBE`` / ``UNSUBSCRIBE`` with ``ERR unknown command`` (the
pre-sharded behavior) instead of rewriting them to an ``SSUBSCRIBE`` the upstream cannot answer; it
defaults to ``SHARDED``, preserving the transparent rewrite described above.
