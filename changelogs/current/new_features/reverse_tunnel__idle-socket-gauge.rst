Added a per-worker idle socket gauge to the reverse tunnel acceptor's detailed stats,
``<stat_prefix>.<worker>.idle.cluster.<cluster>``: the cluster's sockets a request on that worker can
still take. A request can only use an idle socket of its own worker, so this shows which worker runs out
of a cluster's sockets while others still hold some, which the per-worker connection gauges cannot.
