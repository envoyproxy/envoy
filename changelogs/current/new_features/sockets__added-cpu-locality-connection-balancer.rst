Added :ref:`cpu_locality_balance
<envoy_v3_api_field_config.listener.v3.Listener.ConnectionBalanceConfig.cpu_locality_balance>` to
steer each new TCP connection to the worker thread pinned to the CPU that received it, using a kernel
``SO_REUSEPORT`` BPF program. This removes the lock the exact connection balancer takes on every
accept and keeps each connection on a single worker for cache and ``NUMA`` locality. It requires
:ref:`enable_worker_cpu_affinity
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_worker_cpu_affinity>`, reuse port, a kernel
that supports reuse port BPF steering, and a worker count no greater than the number of CPUs in the
process affinity mask. When any requirement is not met the listener keeps serving with the kernel
default reuse port hashing and without CPU locality. It is available on Linux only.
