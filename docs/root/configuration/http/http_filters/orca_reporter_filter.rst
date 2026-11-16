.. _config_http_filters_orca_reporter:

ORCA Reporter
=============

The ORCA reporter filter adds an ORCA load report with the CPU and memory utilization of one or
more Linux cgroups to every response, in the ``endpoint-load-metrics`` header. It is meant to run
in a sidecar or front proxy next to the application, so that a client Envoy can balance load with
a load aware policy such as :ref:`client side weighted round robin
<envoy_v3_api_msg_extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin>`.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.orca_reporter.v3.OrcaReporter``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.orca_reporter.v3.OrcaReporter>`

.. attention::

  The ORCA reporter filter is under active development. Only cgroup v2 is supported.

How it works
------------

The cgroups are sampled on the main thread every :ref:`refresh_interval
<envoy_v3_api_field_extensions.filters.http.orca_reporter.v3.OrcaReporter.refresh_interval>`, and
the encoded report is published to the workers. Responses only copy the latest report, so the
request path never reads files. Filters with the same configuration share one sampler, so a
configuration reload that doesn't change the filter keeps the CPU baseline.

* CPU utilization is the growth of ``usage_usec`` in ``cpu.stat`` divided by the elapsed time and
  the CPU capacity. It needs two samples, so the first refresh runs 100ms after the filter is
  created.
* Memory utilization is the working set (``memory.current`` minus ``inactive_file`` in
  ``memory.stat``) divided by the memory capacity.
* Without a configured capacity, the limits in ``cpu.max`` and ``memory.max`` are used. If a cgroup
  has no limit, the metric is left out of the report.
* A metric that can't be read is left out of the report. If no metric is available, no header is
  added.

.. note::

  By default, client side weighted round robin uses ``cpu_utilization`` from these reports and
  ignores ``mem_utilization``. To also balance on memory, list both metrics in its :ref:`metric_names_for_computing_utilization
  <envoy_v3_api_field_extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin.metric_names_for_computing_utilization>`,
  for example ``[cpu_utilization, mem_utilization]``. The highest of the listed values is used.

A response that already carries a load report, for example one set by the application, is left
untouched unless :ref:`overwrite_existing_report
<envoy_v3_api_field_extensions.filters.http.orca_reporter.v3.OrcaReporter.overwrite_existing_report>`
is set. A client only uses the first report it sees, so the report is written where the
application is expected to put its own:

* gRPC responses that don't end with their headers get the report in the trailers.
* All other responses get the report in the headers. A report that the application sends only in
  trailers is then ignored by the client.

Cgroup paths
------------

Each cgroup is selected by a :ref:`path <envoy_v3_api_field_extensions.filters.http.orca_reporter.v3.CgroupSource.path>`
that may refer to environment variables as ``{NAME}``. The path is resolved once, when the
configuration is loaded.

To report the utilization of Envoy's own container:

.. code-block:: yaml

  name: envoy.filters.http.orca_reporter
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.orca_reporter.v3.OrcaReporter
    metrics:
    - name: cpu_utilization
      cgroup_cpu:
        cgroups:
        - path: /sys/fs/cgroup
    - name: mem_utilization
      cgroup_memory:
        cgroups:
        - path: /sys/fs/cgroup

To report the utilization of the whole Kubernetes pod, mount the host's ``/sys/fs/cgroup``
read-only into the Envoy container, expose the pod UID through the downward API, and build the
pod's cgroup path from it. The layout depends on the node's cgroup driver and the pod's QoS
class. With the systemd driver, the UID is written with ``_`` instead of ``-``:

.. code-block:: yaml

  # Pod spec
  containers:
  - name: envoy
    env:
    - name: POD_UID
      valueFrom: { fieldRef: { fieldPath: metadata.uid } }
    volumeMounts:
    - { name: host-cgroup, mountPath: /host/cg, readOnly: true }
  volumes:
  - name: host-cgroup
    hostPath: { path: /sys/fs/cgroup, type: Directory }

.. code-block:: yaml

  # Filter
  metrics:
  - name: cpu_utilization
    cgroup_cpu:
      cgroups:
      - path: /host/cg/kubepods.slice/kubepods-burstable.slice/kubepods-burstable-pod{POD_UID}.slice
        value_rewrites:
        - pattern: { regex: "-" }
          substitution: "_"

With the cgroupfs driver the same pod is at ``/host/cg/kubepods/burstable/pod{POD_UID}`` and no
rewrite is needed.
