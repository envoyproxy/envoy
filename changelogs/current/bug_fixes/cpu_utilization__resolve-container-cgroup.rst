Fixed the ``envoy.resource_monitors.cpu_utilization`` monitor in ``CONTAINER`` mode reporting host
CPU usage instead of container CPU usage. The reader assumed the cgroup v2 mount point was the
container's own cgroup, which only holds under a private cgroup namespace. In the host cgroup
namespace the mount point is the cgroup root, whose ``cpu.stat`` covers the whole machine and which
has no ``cpu.max`` at all. The container's cgroup directory is now resolved by joining the cgroup2
mount point from ``/proc/self/mountinfo`` with the unified hierarchy path from
``/proc/self/cgroup``, falling back to the mount point when it cannot be determined. This behavior
can be disabled with the runtime guard
``envoy.reloadable_features.cpu_utilization_resolve_container_cgroup``.
