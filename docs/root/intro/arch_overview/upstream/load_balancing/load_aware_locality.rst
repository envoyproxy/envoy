:orphan:

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
reports to weight each locality by its available headroom, preferring the
local zone when load is balanced and spilling to remote zones as the local
zone heats up.

Choosing this policy
^^^^^^^^^^^^^^^^^^^^

Use this policy when upstream endpoints report ORCA utilization and Envoy
should make cross-zone routing decisions from observed backend load. It is
most useful when traffic should stay local while zones are similarly loaded,
then spill toward remote localities with more available headroom as the local
zone's load rises.

Prefer another policy when:

- **Only local-zone preference is needed.** Zone-aware routing is simpler and
  has no ORCA dependency when traffic is already balanced by other means.
- **The control plane owns locality weights.** Use
  :ref:`WrrLocality
  <envoy_v3_api_msg_extensions.load_balancing_policies.wrr_locality.v3.WrrLocality>`
  with EDS locality weights when locality weighting should be computed
  centrally.
- **Routing must be deterministic.** Use ring hash or Maglev for session
  affinity or consistent hashing. They may also be configured as the
  endpoint-picking child policy of this policy, but see
  :ref:`Caveats <load_aware_locality_caveats>` for the resulting behavior.

Example configuration
^^^^^^^^^^^^^^^^^^^^^

Minimal configuration with round robin endpoint picking:

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

See the proto for the full set of tuning parameters.

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
       Any LB policy may be configured here, including ring hash and Maglev,
       though policies that build cluster-wide structures will operate over
       only the chosen locality's host slice. See
       :ref:`Caveats <load_aware_locality_caveats>`.
   * - ``weight_update_period``
     - 1 s
     - How often locality weights are recomputed from ORCA data. Must be at
       least 100 ms.
   * - ``metric_names_for_computing_utilization``
     - (unset)
     - Named ORCA metrics used to compute utilization when
       ``application_utilization`` is not reported. The max of matching
       values is taken. Map entries use ``<map_field_name>.<map_key>`` (e.g.
       ``named_metrics.foo``). See
       :ref:`Weight computation <load_aware_locality_weight_computation>` for
       precedence.
   * - ``utilization_variance_threshold``
     - 0.1
     - When the local locality's utilization is at most this far above the
       host-count-weighted remote average, all traffic is routed locally.
       One-sided check: if local is less loaded than remote, all-local
       routing always applies. Range: [0, 1].
   * - ``smoothing_time_constant``
     - 5 s
     - EWMA time constant for per-locality utilization smoothing. The
       per-tick smoothing factor is derived as
       ``alpha = 1 - exp(-weight_update_period / smoothing_time_constant)``,
       so settling time is independent of the configured tick rate. Larger
       values produce more stable weights; smaller values react faster.
       Must be greater than 0 s.
   * - ``remote_probe_fraction``
     - 0.03
     - Minimum fraction of traffic sent to non-local localities to keep ORCA
       data fresh in all-local mode. The deficit is redistributed
       proportionally to host count. Set to 0 to disable (safe only with
       out-of-band ORCA reporting or when cross-zone traffic must be strictly
       avoided). Range: [0, 1). Note: at very high remote-locality counts
       combined with low aggregate request rates, per-host sample intervals
       can exceed ``weight_expiration_period``; see
       :ref:`Caveats <load_aware_locality_caveats>`.
   * - ``weight_expiration_period``
     - 180 s
     - Per-host sample validity window. Hosts that have not reported within
       this duration are excluded from their locality's utilization
       aggregation. The locality's EWMA continues over the remaining
       reporting hosts; if every host in a locality is stale, the locality
       falls back to host-count-proportional weighting. Set to 0 s to
       disable expiration.

Architecture
^^^^^^^^^^^^

The policy operates at two levels: locality picking (this policy, by
ORCA-derived headroom) and endpoint picking (a configurable child policy).
The split lets you pair load-aware locality selection with whatever
endpoint-picking strategy fits your workload.

Request path:

::

  Incoming request
    |
    +-- 1. Priority selection  (standard healthy/degraded priority load)
    |
    +-- 2. Locality selection  (this policy: weighted random by ORCA headroom)
    |
    +-- 3. Endpoint selection  (child LB)
    |
    v
  Chosen upstream host

Implementation model
""""""""""""""""""""

The policy is implemented as a ``ThreadAwareLoadBalancer``:

- A main-thread timer recomputes per-locality weights from ORCA data and
  publishes an immutable snapshot to worker threads via a thread-local slot.
  The snapshot carries a generation counter; workers rebuild per-locality
  child LB instances when membership changes bump the generation.
- Worker threads read the latest snapshot lock-free on the request path,
  pick a locality, and delegate endpoint selection to the child LB for that
  locality.
- ORCA reports are stored in per-host ``HostLbPolicyData`` slots. This
  policy and
  :ref:`CSWRR <arch_overview_load_balancing_types_client_side_weighted_round_robin>`
  attach independent entries, so they can consume the same reports without
  interfering with each other.

ORCA data flow
""""""""""""""

Upstream endpoints must report ORCA utilization. The policy currently
consumes in-band ORCA reports (returned on response headers or trailers
of upstream responses). Out-of-band (OOB) gRPC reporting is not yet
integrated; once it is, ``remote_probe_fraction`` may be set to 0 since
utilization will arrive independently of traffic.

Utilization is derived from each host's ORCA report using the same
extraction as CSWRR (precedence may be flipped by the
``envoy.reloadable_features.orca_weight_manager_use_named_metrics_first``
runtime feature). By default:

1. ``application_utilization`` -- value in [0, 1], used when reported and
   greater than 0.
2. Named metrics via ``metric_names_for_computing_utilization`` -- max of
   present values, used when ``application_utilization`` is not reported.
3. ``cpu_utilization`` -- final fallback.

Combining with Client-Side Weighted Round Robin
"""""""""""""""""""""""""""""""""""""""""""""""

:ref:`CSWRR <arch_overview_load_balancing_types_client_side_weighted_round_robin>`
can be used as the ``endpoint_picking_policy``. This enables two-level
ORCA-aware load balancing: locality selection by locality-level headroom,
endpoint selection by per-endpoint capacity weights.

.. _load_aware_locality_weight_computation:

Weight computation
^^^^^^^^^^^^^^^^^^

On each ``weight_update_period`` tick, the main thread recomputes per-
locality routing weights:

::

  # Per-tick smoothing factor (consistent settling regardless of tick rate)
  alpha = 1 - exp(-weight_update_period / smoothing_time_constant)

  # Per-host sample validity filter (excludes hosts whose last ORCA report
  # is older than weight_expiration_period; if disabled, all hosts qualify)
  valid(h)         = (now - last_report_time(h)) <= weight_expiration_period
  valid_hosts(L)   = { h in hosts(L) : valid(h) }

  # Per-locality utilization (EWMA smoothed; first sample applied raw)
  if valid_hosts(L) is empty:
      smoothed_util(L) = unchanged    # carry prior smoothed value (or
                                      # treat L as stale -- see below)
      stale(L) = true
  else:
      raw_util(L)      = avg over h in valid_hosts(L) of util(h)
      smoothed_util(L) = alpha * raw_util(L)
                         + (1 - alpha) * prev_smoothed_util(L)
      stale(L) = false

  # Base headroom weight; stale localities use host_count baseline
  if stale(L):
      base_weight(L) = host_count(L)
  else:
      headroom(L)    = max(0, 1 - smoothed_util(L))
      base_weight(L) = host_count(L) * headroom(L)

  total_base_weight  = sum(base_weight(L_i) for all L_i)
  remote_host_count  = sum(host_count(R_i) for all remote R_i)

  # All-overloaded fallback
  if total_base_weight == 0:
      adjusted_weight(L_i) = host_count(L_i)
  else:
      adjusted_weight(L_i) = base_weight(L_i)

      if local exists and remote_host_count > 0:
          # Local preference (one-sided: local must not be too far ABOVE remote)
          remote_weighted_avg = sum(smoothed_util(R_i) * host_count(R_i))
                                / remote_host_count
          if smoothed_util(local) <= remote_weighted_avg + utilization_variance_threshold:
              adjusted_weight(local) = total_base_weight
              adjusted_weight(R_i) = 0

          # Remote probe enforcement
          total_adjusted_weight = sum(adjusted_weight(L_i) for all L_i)
          remote_weight = sum(adjusted_weight(R_i) for all remote R_i)
          remote_share = remote_weight / total_adjusted_weight
          if remote_share < remote_probe_fraction:
              deficit = remote_probe_fraction * total_adjusted_weight - remote_weight
              adjusted_weight(local) = max(0, adjusted_weight(local) - deficit)
              for each remote R_i:
                  adjusted_weight(R_i) += deficit * host_count(R_i) / remote_host_count

  routing_share(L) = adjusted_weight(L) / sum(adjusted_weight(L_i) for all L_i)

Host-count proportional probe redistribution is intentional: when probing
for fresh data, we sample remote localities fairly rather than biasing
toward zones whose current (possibly stale) utilization happens to look
lower.

Worked example
""""""""""""""

Three localities, default variance threshold 0.1:

- **A** (local): 10 hosts, utilization 0.7
- **B** (remote): 10 hosts, utilization 0.3
- **C** (remote): 10 hosts, utilization 0.4

Host-count-weighted remote average: ``(0.3*10 + 0.4*10) / 20 = 0.35``.
Local (0.7) exceeds ``0.35 + 0.1 = 0.45``, so spillover is active.

Headroom weights: A=3, B=7, C=6, total=16. Traffic split: **A ~19%,
B ~44%, C ~37%** -- traffic flows from the hot local zone toward
localities with more headroom.

If load rebalances and all localities converge to ~0.45, local is within
threshold and the policy snaps to **100% local** (minus the 3% remote
probe). Asymmetric host counts shift the weighted average accordingly: a
larger remote locality pulls the average toward its own utilization.

Local preference and remote probing
""""""""""""""""""""""""""""""""""""

``utilization_variance_threshold`` and ``remote_probe_fraction`` together
balance two goals: minimize cross-zone traffic, keep remote ORCA data
fresh.

- **All-local trigger.** When local utilization is at most
  ``utilization_variance_threshold`` above the host-count-weighted remote
  average, the policy routes 100% locally. One-sided: if local is less
  loaded than remote, all-local always applies regardless of gap size.
- **Probe floor.** Even in all-local mode, ``remote_probe_fraction``
  ensures a minimum slice of traffic still reaches remote localities so
  ORCA data does not go stale. The deficit is taken from the local
  weight (clamped at 0) and distributed across remotes proportional to
  host count -- not headroom -- to sample all remotes fairly.
- **Disabling probing.** Set ``remote_probe_fraction`` to 0 only if OOB
  ORCA reporting is available or zero cross-zone traffic is required.
  Without probing, the policy reacts slowly to remote load changes.

.. _load_aware_locality_weight_expiration:

Weight expiration
"""""""""""""""""

``weight_expiration_period`` (default 3 minutes) controls per-host sample
validity. Hosts that have not produced an ORCA report within this window
are excluded from their locality's utilization aggregation for the
current tick. The locality's EWMA state continues normally over the
remaining reporting hosts -- there is no synthetic reset and no transient
traffic surge.

If every host in a locality is stale, the locality falls back to
host-count-proportional weighting (the same path used by the
all-overloaded fallback). This keeps traffic flowing to localities that
have stopped reporting without artificially boosting them: they receive
their fair share of host-count weight, no more.

Tune higher to tolerate longer reporting gaps; tune lower to prune
draining backends faster. Set to 0 s to disable expiration entirely.

Cold-start behavior
"""""""""""""""""""

At startup (or with no ORCA data) every locality defaults to utilization
0, full headroom, so weights reduce to host counts -- equivalent to
round-robin locality selection. The first ORCA sample for each locality
is applied raw (no EWMA blending), so the policy begins differentiating
within a single ``weight_update_period`` cycle.

Priority support
""""""""""""""""

The policy respects Envoy's
:ref:`priority levels <arch_overview_load_balancing_priority_levels>`.
Priority selection happens first via the standard healthy/degraded
priority load calculation; locality selection then applies within the
chosen priority. Unlike zone-aware routing (priority 0 only), this
policy applies at all priority levels.

Three independent weight sets are maintained per priority:

- **Healthy** -- common case, healthy hosts only.
- **Degraded** -- when Envoy selects
  :ref:`degraded <arch_overview_load_balancing_degraded>` hosts.
- **All-host** -- when the priority is in
  :ref:`panic mode <arch_overview_load_balancing_panic_threshold>`.

Each set tracks its own per-locality utilization average and headroom
weight, computed from the same per-host ORCA data in a single tick pass.

.. _load_aware_locality_caveats:

Caveats and known limitations
"""""""""""""""""""""""""""""

- **Hash-based child policies.** Ring hash and Maglev build their hash
  structures from the host set they are given. With this policy, that set
  is the chosen locality's hosts, not the full cluster. The same request
  hash will not necessarily map to the same endpoint cluster-wide, so
  consistency guarantees apply only within a locality.
- **Probe-fraction scaling.** ``remote_probe_fraction`` is a global value
  divided across all remote localities. With many remote localities and
  low aggregate request rates, the per-host sample interval can exceed
  ``weight_expiration_period``. Approximate sample intervals at the
  default 0.03 probe fraction:

  +-----------+-----------+----------------+--------------+
  | Total RPS | N remotes | Hosts/locality | Sample/host  |
  +===========+===========+================+==============+
  | 1000      | 3         | 10             | ~1 s         |
  +-----------+-----------+----------------+--------------+
  | 1000      | 100       | 10             | ~33 s        |
  +-----------+-----------+----------------+--------------+
  | 100       | 100       | 10             | ~5.5 min     |
  +-----------+-----------+----------------+--------------+

  In the bottom row, samples expire before the next probe arrives. Either
  reduce locality count, increase probe fraction, raise
  ``weight_expiration_period``, or wait for OOB ORCA reporting.
- **Variance-threshold oscillation.** Workloads sitting near the
  ``utilization_variance_threshold`` boundary can theoretically oscillate
  between snap-to-local and spillover modes across consecutive ticks.
  EWMA smoothing dampens this in practice; tune
  ``smoothing_time_constant`` higher if oscillation is observed.
- **Subsetting.** Load balancer
  :ref:`subsetting <arch_overview_load_balancer_subsets>` partitions
  hosts orthogonally to locality boundaries. The policy will operate
  over the post-subset host slice; per-locality weights are computed
  over whatever hosts remain after subsetting filters them. This is
  rarely the behavior subset users expect.

Statistics
""""""""""

The policy emits stats under ``cluster.<cluster_name>.load_aware_locality.*``:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Counter
     - Increments when
   * - ``recompute_total``
     - Per main-thread tick that recomputes weights.
   * - ``all_overloaded_total``
     - Per tick where every locality's headroom was 0 (fallback to
       host-count weighting).
   * - ``local_preferred_total``
     - Per tick where the variance-threshold check snapped routing to
       100% local.
   * - ``probe_active_total``
     - Per tick where ``remote_probe_fraction`` redistribution kicked in.
   * - ``stale_locality_total``
     - Per tick per locality whose hosts were all stale (fell back to
       host-count baseline for that tick).

Migrating from zone-aware routing? The closest counter mappings:

+--------------------------------------+----------------------------------------------+
| Zone-aware counter                   | Load-aware-locality equivalent               |
+======================================+==============================================+
| ``lb_zone_routing_all_directly``     | ``load_aware_locality.local_preferred_total``|
+--------------------------------------+----------------------------------------------+
| ``lb_recalculate_zone_structures``   | ``load_aware_locality.recompute_total``      |
+--------------------------------------+----------------------------------------------+

.. _load_aware_locality_comparison:

Comparison with other approaches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy offers three locality-selection strategies. The right choice
depends on whether ORCA reporting is available, whether the control
plane supplies locality weights, and whether the deployment must react
to runtime load imbalance.

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
| Cross-zone traffic minimization        | Yes (local preference)        | Depends on weights            | Yes (local preference + probe floor) |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Cold-start behavior                    | Routes by host count ratio    | Routes by EDS weights         | Routes proportionally to host count  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Oscillation dampening                  | N/A                           | N/A                           | EWMA (time-constant smoothing)       |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Control plane dependency               | None                          | Requires EDS weights          | None (data-plane only)               |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Priority level support                 | P=0 only                      | All                           | All                                  |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Degraded / panic mode support          | No                            | Yes                           | Yes (separate weight sets)           |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
| Load balancer subsetting               | Yes                           | No                            | Documented limitation                |
+----------------------------------------+-------------------------------+-------------------------------+--------------------------------------+
