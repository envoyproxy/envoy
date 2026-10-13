Added a new
:ref:`per_worker_subset <extension_envoy.load_balancing_policies.per_worker_subset>`
load balancing policy. Each Envoy worker maintains its own subset of upstream hosts and
load-balances requests across only that subset, decoupling per-worker connection-pool size from
total cluster size. Useful for large upstream clusters where the upstream enforces a short
server-side HTTP keepalive timeout. Two partitioning strategies (``EQUAL_PARTITIONS`` and
``RANDOM_PARTITIONS``) can be combined with three within-subset host-selection strategies
(``SIMPLE_ROUND_ROBIN``, ``ENVOY_ROUND_ROBIN`` delegating to the stock RoundRobin LB, and
``ENVOY_P2C`` delegating to the stock LeastRequest LB). Per-worker fallback eliminates synchronized
cluster-wide connection-pool churn that a cluster-wide healthy-fraction check would produce.
