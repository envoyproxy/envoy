.. _arch_overview_load_balancing_load_aware_locality:

Load-aware locality load balancing
-----------------------------------

.. attention::

  This extension is **alpha** and is not yet intended for production use.

The load-aware locality LB policy
(:ref:`envoy.load_balancing_policies.load_aware_locality
<envoy_v3_api_msg_extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality>`)
is a locality-picking load balancer designed for deployments where incoming
load is not evenly distributed across zones, causing some localities to run
hotter than others. It uses per-endpoint utilization from
`ORCA <https://github.com/cncf/xds/blob/main/proposals/A51-custom-lb-policies.md>`_
reports to weight each locality by its available headroom,
preferring the local zone when load is balanced and spilling to
remote zones as the local zone heats up.

When to use this policy
^^^^^^^^^^^^^^^^^^^^^^^

This policy is a good fit when:

- You want cross-zone traffic decisions driven by actual backend load rather
  than static host counts or management-server-provided weights.
- You want local-zone preference when things are balanced, but automatic
  spillover when they are not.

When not to use this policy
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This policy is **not** a good fit when:

- **You need deterministic routing.** For session affinity or consistent
  hashing, use ring hash or Maglev instead.
- **Traffic is already balanced and you just want local preference.**
  Zone-aware routing is simpler, well-tested, and has no ORCA dependency.
- **You want the control plane to dictate routing weights.** Use the
  :ref:`WrrLocality
  <envoy_v3_api_msg_extensions.load_balancing_policies.wrr_locality.v3.WrrLocality>`
  policy with EDS locality weights instead.

Example configuration
^^^^^^^^^^^^^^^^^^^^^

Minimal configuration using round robin as the endpoint-picking child policy:

.. code-block:: yaml

  load_balancing_policy:
    policies:
    - typed_extension_config:
        name: envoy.load_balancing_policies.load_aware_locality
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality
          endpoint_picking_policy:
            policies:
            - typed_extension_config:
                name: envoy.load_balancing_policies.round_robin
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin

Full configuration with all tuning parameters:

.. code-block:: yaml

  load_balancing_policy:
    policies:
    - typed_extension_config:
        name: envoy.load_balancing_policies.load_aware_locality
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality
          endpoint_picking_policy:
            policies:
            - typed_extension_config:
                name: envoy.load_balancing_policies.least_request
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.least_request.v3.LeastRequest
          weight_update_period: 2s
          utilization_variance_threshold: 0.15
          ewma_alpha: 0.4
          probe_percentage: 0.05
          weight_expiration_period: 120s

Configuration parameters
^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 35 10 55

   * - Parameter
     - Default
     - Description
   * - ``endpoint_picking_policy``
     - (required)
     - Child LB policy for selecting an endpoint within the chosen locality.
       Any endpoint-picking policy can be used, regardless of wehther the policy
       implements ORCA handling (e.g. least request).
   * - ``weight_update_period``
     - 1 s
     - How often locality weights are recomputed from ORCA data.
   * - ``metric_names_for_computing_utilization``
     - (unset)
     - Named ORCA metrics to use for computing utilization. When configured,
       the max of the matching metric values is used. Map field entries use
       the form ``<map_field_name>.<map_key>`` (e.g. ``named_metrics.foo``).
       If not configured or no named metrics match, ``application_utilization``
       is used; if that is also absent or zero, ``cpu_utilization`` is used.
       See :ref:`Weight computation <load_aware_locality_weight_computation>`
       for the full precedence order.
   * - ``utilization_variance_threshold``
     - 0.1
     - When the local locality's utilization is at most this value above the
       host-count-weighted remote average, all traffic is routed locally. This
       is a one-sided check -- if the local zone is less loaded than remote
       zones, all-local routing always applies. Range: [0, 1].
   * - ``ewma_alpha``
     - 0.3
     - EWMA smoothing factor. Higher values react faster; lower values are more
       stable. Range: (0, 1].
   * - ``probe_percentage``
     - 0.03
     - Minimum fraction of traffic sent to non-local localities to keep ORCA data
       fresh. The deficit is redistributed proportionally to host count. Set to
       0 to disable (safe only if out-of-band ORCA reporting is available or if
       zero cross-zone traffic is strictly required). Range: [0, 1).
   * - ``weight_expiration_period``
     - 180 s
     - ORCA samples older than this are discarded and the locality's EWMA state
       is reset. Set to 0 s to disable expiration.

Architecture
^^^^^^^^^^^^

The policy operates at two levels:

1. **Locality picking** (this policy) -- selects which locality to route a given
   request to, based on ORCA-derived utilization.
2. **Endpoint picking** (a configurable child policy) -- selects the specific
   endpoint within the chosen locality. Any endpoint-picking LB policy
   (e.g. client-side weighted round robin or least request) can be used.

This separation means you can pair load-aware locality selection with whatever
endpoint-picking strategy best suits your workload. The locality-picking layer
handles *where* to send traffic; the child policy handles *which host* within
that locality.

At a high level, the request path looks like this:

::

  Incoming request
    |
    +-- 1. Priority selection  (standard Envoy healthy/degraded priority load)
    |
    +-- 2. Locality selection  (this policy: weighted random by ORCA headroom)
    |
    +-- 3. Endpoint selection  (child LB: e.g. round robin, least request)
    |
    v
  Chosen upstream host

Threading model
"""""""""""""""

The policy is implemented as a ``ThreadAwareLoadBalancer``:

- **Main thread:** A periodic timer (configurable via ``weight_update_period``)
  reads ORCA utilization from each healthy host, averages utilization
  per locality, applies EWMA smoothing, and computes capacity-weighted routing
  weights. The resulting immutable snapshot is published to worker threads via a
  thread-local slot.
- **Worker threads:** Each worker maintains per-locality child LB instances.
  On the request path, the worker reads the latest weight snapshot from its
  thread-local slot, performs weighted random locality selection, and delegates
  endpoint selection to the chosen locality's child LB.

This design means the request path involves no locks or cross-thread
synchronization -- workers only read from their thread-local slot.

ORCA data flow
""""""""""""""

For this policy to function, upstream endpoints must report ORCA utilization
data. ORCA reports can be delivered in two ways:

- **Per-request reports:** Utilization data is piggybacked on response trailers.
  The Envoy router processes these reports and stores the utilization value in a
  per-host slot so that the main-thread weight computation can read it without
  locking. This is the mechanism used by this policy today.
- **Out-of-band (OOB) reports:** A periodic gRPC stream from the backend to
  Envoy. OOB reporting is not yet integrated with this policy but could
  eliminate the need for ``probe_percentage`` in the future, since utilization
  data would arrive independently of traffic.

Because the policy currently relies on per-request reports, it can only receive
fresh utilization data from localities that are actively receiving traffic. This
is why the ``probe_percentage`` parameter exists: it ensures a minimum fraction
of traffic is sent to non-local localities to keep their ORCA data fresh.

Endpoints should report at least one of:

- Named metrics via ``metric_names_for_computing_utilization`` -- when
  configured, these take highest precedence.
- ``application_utilization`` -- a value in [0, 1] representing
  application-level load. Used when no named metrics are configured or present.
- ``cpu_utilization`` -- used as final fallback when neither named metrics nor
  ``application_utilization`` are available.

**Combining with Client-Side Weighted Round Robin**

:ref:`Client-Side Weighted Round Robin (CSWRR)
<arch_overview_load_balancing_types_client_side_weighted_round_robin>` can be
used as the ``endpoint_picking_policy`` without conflict. This policy (for
locality selection) and CSWRR (for endpoint selection within a locality) both
consume ORCA data independently via separate per-host slots. Utilization reports
reach both policies without interference, enabling two-level ORCA-aware load
balancing: locality selection is driven by locality-level headroom, and endpoint
selection within each locality is driven by per-endpoint capacity weights.

.. _load_aware_locality_weight_computation:

Weight computation
^^^^^^^^^^^^^^^^^^

On the main thread, a periodic timer (configurable via ``weight_update_period``,
default 1 s) recomputes per-locality routing weights using the following steps:

1. For each locality, compute the average utilization of its endpoints from their
   most recent ORCA reports. The metric precedence order is:

   a. The max of any metrics listed in ``metric_names_for_computing_utilization``
      that are present in the report (e.g. ``named_metrics.foo``), if configured.
   b. ``application_utilization`` if reported and > 0.
   c. ``cpu_utilization``.

2. Apply EWMA smoothing (controlled by ``ewma_alpha``, default 0.3) to dampen
   oscillation. On the very first report for a locality (or after EWMA state has
   been reset due to weight expiration), the raw utilization value is used
   directly without blending, so the policy begins differentiating localities
   after a single update cycle.
3. Weight each locality proportionally to ``host_count * (1 - smoothed_utilization)``.
4. If a local locality exists, compute the **host-count-weighted** average
   utilization of all remote localities. If the local locality's utilization is
   **at most** ``utilization_variance_threshold`` (default 0.1) **above** this
   remote average, route 100 % of traffic locally to avoid unnecessary
   cross-zone hops. Note that this is a one-sided check: if the local zone is
   *less* loaded than the remote average (by any amount), all-local routing
   always triggers. The threshold only governs how much *hotter* the local zone
   can be before spillover begins.
5. Enforce a ``probe_percentage`` (default 3 %) minimum share of traffic to
   non-local localities so ORCA data stays fresh even when local routing is
   dominant. When the remote localities' combined weight is below this minimum,
   the deficit is subtracted from the local locality's weight and redistributed
   to remote localities **proportionally to their host count** (not
   proportionally to their headroom weights). Host-count proportional
   redistribution is intentional: when probing for fresh data, we want to sample
   all remote localities fairly rather than biasing toward zones whose current
   (possibly stale) utilization happens to look lower.
6. If all localities report zero headroom (utilization >= 1.0), fall back to
   distributing traffic proportionally to host count.

These weights are published to worker threads via a thread-local slot.
Each worker thread maintains per-locality child LB instances and uses the
published weights to perform weighted random locality selection on the
request path.

In pseudo-formula notation:

::

  # Per-locality utilization (EWMA smoothing)
  smoothed_util(L) = ewma_alpha * raw_util(L) + (1 - ewma_alpha) * prev_smoothed_util(L)
                      (first sample: smoothed_util(L) = raw_util(L))

  # Per-locality headroom and weight
  headroom(L)      = max(0, 1 - smoothed_util(L))
  weight(L)        = host_count(L) * headroom(L)
  routing_share(L) = weight(L) / sum(weight(L_i) for all L_i)

  # Local-preference check (one-sided: local must not be too far ABOVE remote)
  remote_weighted_avg = sum(smoothed_util(R_i) * host_count(R_i)) / sum(host_count(R_i))
  if smoothed_util(local) <= remote_weighted_avg + utilization_variance_threshold:
      route 100% to local (subject to probe_percentage below)

  # Probe percentage enforcement
  remote_share = sum(weight(R_i)) / sum(weight(L_i) for all L_i)
  if remote_share < probe_percentage:
      deficit = probe_percentage * total_weight - sum(weight(R_i))
      weight(local) -= deficit
      for each remote R_i:
          weight(R_i) += deficit * host_count(R_i) / sum(host_count(R_j))

  # All-overloaded fallback
  if sum(weight(L_i)) == 0:
      weight(L_i) = host_count(L_i)    # proportional to host count

Worked example
""""""""""""""

Consider 3 localities with the default variance threshold of 0.1:

- **Locality A** (local): 10 hosts, average utilization 0.7
- **Locality B** (remote): 10 hosts, average utilization 0.3
- **Locality C** (remote): 10 hosts, average utilization 0.4

The host-count-weighted remote average utilization is
``(0.3 * 10 + 0.4 * 10) / (10 + 10) = 0.35``. Locality A's utilization (0.7)
exceeds the remote average plus the threshold (``0.35 + 0.1 = 0.45``), so the
policy does **not** route 100 % locally.

Headroom-weighted routing is computed as:

- A: ``10 * (1 - 0.7) = 3``
- B: ``10 * (1 - 0.3) = 7``
- C: ``10 * (1 - 0.4) = 6``
- Total: ``3 + 7 + 6 = 16``

Traffic split: **A ~ 19 %, B ~ 44 %, C ~ 37 %**. Traffic flows away from the
hot local zone toward localities with more headroom.

Now suppose load rebalances and all localities converge to roughly 0.45
utilization. The remote average is 0.45, and locality A is within the variance
threshold (``0.45 <= 0.45 + 0.1``). The policy snaps to **100 % local
routing** (minus the 3 % probe percentage), avoiding unnecessary cross-zone
hops.

Asymmetric host count example
'''''''''''''''''''''''''''''

The host-count-weighted remote average matters when localities have different
sizes:

- **Locality A** (local): 10 hosts, utilization 0.50
- **Locality B** (remote): 20 hosts, utilization 0.40
- **Locality C** (remote): 5 hosts, utilization 0.60

The host-count-weighted remote average is
``(0.40 * 20 + 0.60 * 5) / (20 + 5) = 0.44``. A simple (unweighted) average
would be ``(0.40 + 0.60) / 2 = 0.50`` -- a very different value. Because
locality A's utilization (0.50) is within the variance threshold of the weighted
remote average (``0.50 <= 0.44 + 0.1 = 0.54``), the policy routes **100 %
locally** (minus probe percentage).

Headroom weights if spillover were active:

- A: ``10 * (1 - 0.50) = 5.0``
- B: ``20 * (1 - 0.40) = 12.0``
- C: ``5 * (1 - 0.60) = 2.0``

Notice that locality B's larger host count amplifies its weight even though its
per-host headroom (0.60) is less than A's (0.50).

Local preference and probe percentage
""""""""""""""""""""""""""""""""""""""

The ``utilization_variance_threshold`` and ``probe_percentage`` parameters work
together to balance two goals: minimizing cross-zone traffic and maintaining
fresh ORCA data.

- When the local zone's utilization is **at most** ``utilization_variance_threshold``
  **above** the host-count-weighted remote average, the policy routes 100 % of
  traffic locally. This is a one-sided check: if the local zone is less loaded
  than remote zones, all-local routing always applies regardless of how large
  the gap is. The threshold only limits how much *hotter* the local zone can be
  before spillover begins.
- Even in this "all-local" mode, ``probe_percentage`` (default 3 %) ensures
  that a small fraction of traffic still reaches remote localities. The
  implementation subtracts the deficit from the local locality's weight and
  distributes it to remote localities proportionally to their host count (not
  headroom). Without this probing, the policy would have no fresh ORCA data for
  remote zones and would be blind to load changes there.
- Setting ``probe_percentage`` to 0 disables probing entirely. This is safe
  only if out-of-band ORCA reporting is available or if cross-zone traffic must
  be strictly avoided; be aware that the policy may react slowly to remote load
  changes without it.

Weight expiration
"""""""""""""""""

If an endpoint has not reported ORCA metrics within ``weight_expiration_period``
(default 3 min), its utilization sample is discarded. Additionally, the
per-locality EWMA state is **reset**: the smoothed utilization returns to 0.0 and
the locality appears to have **full headroom** (0 % utilization) until fresh
data arrives. Operators should be aware that this can cause a transient traffic
surge toward a locality whose data has just expired. Tuning
``weight_expiration_period`` higher reduces the chance of spurious resets, while
tuning it lower prevents stale data from persisting after backends are drained
or stop reporting.

Cold-start behavior
"""""""""""""""""""

When the policy first starts (or when no ORCA data is available), each locality's
utilization defaults to 0, giving it full headroom (``1 - 0 = 1.0``). With all
localities at full headroom, routing weights reduce to host counts, making traffic
distribution proportional to the number of hosts in each locality -- equivalent
to round-robin locality selection. As ORCA reports arrive, the first utilization
sample for each locality is applied directly (without EWMA blending), so the
policy begins differentiating localities within a single ``weight_update_period``
cycle rather than gradually ramping through EWMA.

Priority support
""""""""""""""""

The policy respects Envoy's :ref:`priority levels
<arch_overview_load_balancing_priority_levels>`. Priority selection happens first
using the standard healthy/degraded priority load calculation, then locality
selection applies within the chosen priority. Unlike zone-aware routing (which
only operates at priority 0), this policy applies locality-aware selection at
all priority levels.

The policy computes **three separate sets** of per-locality weights for each
priority level:

- **Healthy weights** -- used when the priority load calculation selects healthy
  hosts for this priority. This is the common case.
- **Degraded weights** -- used when Envoy selects
  :ref:`degraded <arch_overview_load_balancing_degraded>` hosts for this
  priority. The weights are computed from the degraded host subset only.
- **All-host weights** -- used when the priority is in
  :ref:`panic mode <arch_overview_load_balancing_panic_threshold>` and Envoy
  falls back to considering all hosts regardless of health status. The weights
  are computed from the full host set.

Each set is computed independently: the per-locality utilization averages,
EWMA smoothing state, and resulting headroom weights are tracked separately for
healthy, degraded, and all-host subsets.

Compatibility notes
"""""""""""""""""""

This policy is **not** currently compatible with :ref:`load balancer subsetting
<arch_overview_load_balancer_subsets>`. Subsetting partitions hosts into subsets
that cut across locality boundaries, and it is not straightforward to reconcile
locality-level headroom weights with per-subset host partitioning.

Statistics
""""""""""

The policy emits the following zone routing statistics under the cluster's stat
prefix (``cluster.<cluster_name>.<stat>``). These are the same counters used by
zone-aware routing, making it easy to compare behavior when migrating between
the two approaches.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Statistic
     - Description
   * - ``lb_zone_routing_all_directly``
     - Request was routed to the local zone while the variance threshold
       triggered all-local routing (i.e. load was balanced).
   * - ``lb_zone_routing_sampled``
     - Request was routed to the local zone via weighted random selection
       (i.e. the local zone won the weighted draw during spillover).
   * - ``lb_zone_routing_cross_zone``
     - Request was routed to a remote zone.

Additionally, ``lb_recalculate_zone_structures`` is incremented each time the
main-thread timer fires and recomputes locality weights.

.. _load_aware_locality_comparison:

Comparison with other approaches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy offers three locality-selection strategies. The right choice depends on
whether you have ORCA reporting, whether the control plane supplies locality
weights, and whether you need to react to runtime load imbalance.

+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Feature                                | Zone-aware routing            | WrrLocality                   | Load-aware locality (this policy)    |
+========================================+===============================+===============================+======================================+
| Routing signal                         | Healthy host counts           | Static weights from EDS       | Real-time ORCA utilization           |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Reacts to load imbalance               | No                            | No                            | Yes                                  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Requires management server weights     | No                            | Yes                           | No                                   |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Requires ORCA reports from backends    | No                            | No                            | Yes                                  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Cross-zone traffic minimization        | Yes (local preference)        | Depends on weights            | Yes (local preference with probe     |
|                                        |                               |                               | minimum)                             |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Cold-start behavior                    | Routes by host count ratio    | Routes by EDS weights         | Routes proportionally to host count  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Oscillation dampening                  | N/A                           | N/A                           | EWMA smoothing                       |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Control plane dependency               | None                          | Requires EDS weights          | None (data-plane only)               |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Priority level support                 | P=0 only                      | All                           | All                                  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Degraded / panic mode support          | No                            | Yes                           | Yes (separate weight sets)           |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Load balancer subsetting               | Yes                           | No                            | No                                   |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+

Implementation notes
^^^^^^^^^^^^^^^^^^^^^

This section describes implementation internals for contributors to this policy.

The policy is implemented as a ``ThreadAwareLoadBalancer``. On the main thread,
a ``weight_update_timer_`` fires periodically to invoke
``computeLocalityRoutingWeights()``, which reads ``host->lbPolicyData()`` from
each healthy host (one slot per locality-picker policy), averages utilization
per locality, applies EWMA smoothing, computes headroom weights, and publishes
an immutable ``RoutingWeightsSnapshot`` to workers via a
``ThreadLocal::TypedSlot<ThreadLocalShim>``.

::

  LoadAwareLocalityLoadBalancer (main thread)
    |
    |-- weight_update_timer_
    |     Fires periodically to recompute locality routing weights.
    |
    |-- on timer callback:
    |     computeLocalityRoutingWeights()
    |       - Read host->lbPolicyData() from each healthy host
    |       - Average per locality, apply EWMA smoothing
    |       - Compute weight = host_count * (1 - smoothed_utilization)
    |       - Check variance threshold for local-zone preference
    |       - Apply probe percentage minimum
    |       - Publish immutable snapshot to workers via TLS
    |
    +-- WorkerLocalLbFactory (shared across all workers)
          |
          |-- child_thread_aware_lb_ (shared child ThreadAwareLoadBalancer)
          |
          |-- tls_ (ThreadLocal::TypedSlot<ThreadLocalShim>)
          |     Main thread pushes RoutingWeightsSnapshot to workers.
          |     Workers read with zero synchronization.
          |
          +-- create() --> WorkerLocalLb (one per worker thread)
                |
                |-- selectLocality() [weighted random by capacity]
                |
                +-- per_locality_[] (one PerLocalityState per locality)
                      |-- PrioritySetImpl (hosts for this locality only)
                      +-- LoadBalancer (worker-local child, e.g. RoundRobin)

ORCA reports arrive via the router filter which calls ``onOrcaLoadReport()`` on
the host's ``lbPolicyData()`` slot for this policy (a separate slot from any
CSWRR slot on the same host). Utilization values are stored via lock-free
atomics (``std::atomic<double>``) so the main-thread weight computation can
read them without locking. This is why ``probe_percentage`` is needed: because
ORCA data arrives only on the request path, remote localities that receive no
traffic yield no data.
