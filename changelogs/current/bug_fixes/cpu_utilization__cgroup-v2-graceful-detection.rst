Fixed a crash loop for the ``envoy.resource_monitors.cpu_utilization`` monitor in ``CONTAINER``
mode on cgroup v2 hosts. Detection of the cgroup v2 CPU controller is now keyed only on
``cpu.stat`` instead of also requiring ``cpu.max`` and ``cpuset.cpus.effective``, which are
frequently absent on Kubernetes pods (no CPU limit set, or the cpuset controller not delegated
into the pod's leaf cgroup). When ``cpuset.cpus.effective`` is absent the reader now falls back
to the host's online CPU count, and an absent ``cpu.max`` is treated as "no CPU limit". If no
supported cgroup CPU implementation is found at all, the monitor now falls back to reporting zero
utilization instead of aborting server startup, so this optional overload input can no longer take
down the data plane at boot.
