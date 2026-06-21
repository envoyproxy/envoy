Fixed container-aware CPU limit detection (#45410) not being enabled by
default. The minimum of the cgroup CPU limit, CPU affinity, and hardware
thread count, added in #40997 and documented as the default in the v1.37.0
release notes, was only applied when ``--cpuset-threads`` was set. It is now
applied whenever ``--concurrency`` is not set, so worker threads are sized to
the cgroup CPU limit in containerized deployments without requiring
``--cpuset-threads``. Detection can still be disabled by setting
``ENVOY_CGROUP_CPU_DETECTION`` to ``false``.
