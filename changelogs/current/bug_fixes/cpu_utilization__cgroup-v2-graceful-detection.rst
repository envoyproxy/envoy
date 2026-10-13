Fixed the ``envoy.resource_monitors.cpu_utilization`` monitor in ``CONTAINER`` mode when Envoy shares
the host cgroup namespace, for example in a privileged container. ``/sys/fs/cgroup`` is then the
cgroup v2 root, which has no ``cpu.max``, so detection failed and config initialization aborted
the server. The monitor now resolves its own cgroup from ``/proc/self/cgroup`` and
``/proc/self/mountinfo`` and reports container rather than host usage. cgroup v2 detection is keyed
on ``cpu.stat``: an absent ``cpu.max`` means no CPU limit and an absent ``cpuset.cpus.effective``
falls back to the CPU affinity count, while a file that exists but cannot be read fails the sample.
