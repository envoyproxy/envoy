Added :ref:`enable_worker_cpu_affinity
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_worker_cpu_affinity>` to pin each worker
thread to a distinct CPU from the process affinity mask, worker ``i`` to the ``i-th`` CPU in
ascending order. This improves CPU cache and ``NUMA`` locality for high concurrency deployments on
bare metal. It is available on Linux only and is ignored on other platforms.
