Fixed excessive CPU usage in the :ref:`Redis proxy
<config_network_filters_redis_proxy>` when cluster-scope commands such as ``SELECT``, ``KEYS`` or
``SCAN`` are routed to a cluster that does not use the ``envoy.clusters.redis`` cluster type. Such
a cluster's load balancer ignores the Redis shard index, so determining the shard count previously
performed one host selection per Redis hash slot (16384) on every cluster-scope command. The shard
count is now taken from the number of healthy hosts in the cluster, which returns the same value
without the per-slot host selections. Clusters using the ``envoy.clusters.redis`` cluster type are
unaffected.
