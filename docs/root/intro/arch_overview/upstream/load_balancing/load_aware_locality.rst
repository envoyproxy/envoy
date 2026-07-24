.. _arch_overview_load_balancing_load_aware_locality:

Load-aware locality load balancing
-----------------------------------

.. attention::

  This extension is **work-in-progress**. Functionality is incomplete and it
  is not intended for production use.

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

.. _load_aware_locality_comparison:

Choosing this policy
^^^^^^^^^^^^^^^^^^^^

Use this policy when upstream endpoints report ORCA utilization and Envoy
should make cross-zone routing decisions from observed backend load. It is
most useful when traffic should stay local while zones are similarly loaded,
then spill toward remote localities with more available headroom as the
local zone's load rises.

Envoy offers three locality-selection strategies. The right choice depends
on whether ORCA reporting is available, whether the control plane supplies
locality weights, and whether the deployment must react to runtime load
imbalance.

- **Pick this policy when** upstream endpoints emit ORCA utilization and
  routing should react to runtime load imbalance from the data plane,
  with no control-plane involvement in locality weighting.
- **Pick zone-aware routing when** only local-zone preference is needed
  and traffic is already balanced by other means. It has no ORCA
  dependency and is simpler to operate, but it only applies at priority
  0 and does not react to backend load. Zone-aware routing is not
  extended here because it is priority-0 only, recomputes synchronously
  per-worker inside membership callbacks (no timer, EWMA, or
  cross-thread snapshot), and is built around a local-cluster percentage
  model that would require rewriting the base class shared by
  round-robin, least-request, and random. This policy requires
  all-priority support, async main-thread ORCA recompute, and no
  local-cluster dependency.
- **Pick**
  :ref:`WrrLocality
  <envoy_v3_api_msg_extensions.load_balancing_policies.wrr_locality.v3.WrrLocality>`
  **when** the control plane owns locality weights and should compute
  them centrally via EDS. Weights are static between updates and do not
  react to runtime load.
- **For deterministic routing** (session affinity, consistent hashing),
  use ring hash or Maglev. They can also be configured as the
  endpoint-picking child policy of this policy, but see
  :ref:`Caveats <load_aware_locality_caveats>` for the resulting behavior.

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
  The snapshot carries only advisory per-priority locality weights; it does
  not carry priority loads, panic state, or health information.
- Workers rebuild per-locality child LB instances via a membership-update
  callback registered on the cluster priority set, independent of the
  weight snapshot and ``weight_update_period``.
- Priority selection, health/degraded mode, and panic detection use standard
  ``LoadBalancerBase`` machinery over live worker-thread host-set state,
  recomputed on membership and health callbacks. Failover between priorities
  is therefore callback-fresh, not timer-driven.
- Worker threads read the latest locality-weight snapshot lock-free on the
  request path, pick a locality, and delegate endpoint selection to the
  child LB for that locality.
- ORCA reports flow through per-host ``HostLbPolicyData`` slots shared
  with other ORCA consumers; see :ref:`ORCA data flow
  <load_aware_locality_orca_data_flow>` below for coexistence details.

.. _load_aware_locality_orca_data_flow:

ORCA data flow
""""""""""""""

Upstream endpoints must report ORCA utilization in-band: ORCA reports are
returned on the response headers or trailers of upstream responses. Sample
rate is tied to the request rate to each host, so probing
(``remote_probe_fraction``) is required to keep remote-locality data fresh.

Reports land in per-host ``HostLbPolicyData`` slots, which feed weight
computation.

Pairing this policy with
:ref:`CSWRR <arch_overview_load_balancing_types_client_side_weighted_round_robin>`
as ``endpoint_picking_policy`` yields two-level ORCA-aware balancing:
locality selection by aggregate headroom, endpoint selection by
per-endpoint capacity. Each consumer attaches independent
``HostLbPolicyData`` entries, so the two policies do not interfere.

Utilization is derived from each host's ORCA report using the same
extraction as CSWRR, which takes the first source whose value is greater
than 0. The runtime flag
``envoy.reloadable_features.orca_weight_manager_use_named_metrics_first``
defaults to ``true``, so the default precedence is:

1. Named metrics via ``metric_names_for_computing_utilization`` -- max of
   present values.
2. ``application_utilization``.
3. ``cpu_utilization`` -- final fallback.

Disabling the runtime flag reverses steps 1 and 2 so
``application_utilization`` is preferred over named metrics.

A report whose derived utilization is not greater than 0 does not count as a
fresh sample: the host is treated as non-reporting and ages out per
``weight_expiration_period``. Note that a configured named metric that
legitimately reads exactly 0 is therefore treated as no signal.

.. _load_aware_locality_weight_computation:

Weight computation
^^^^^^^^^^^^^^^^^^

On each ``weight_update_period`` tick, the main thread recomputes per-
locality routing weights in five stages:

1. **Filter and average.** Drop hosts whose last ORCA report is older than
   ``weight_expiration_period`` and average utilization across the remaining
   hosts in each locality. The locality's EWMA state continues unchanged
   over the remaining reporters -- there is no synthetic reset. If every
   host in a locality is stale, the locality is marked stale and falls
   back to host-count weighting in stage 3.
2. **Smooth.** Apply EWMA smoothing per locality. The first sample for a
   locality is applied raw (no blending) so the policy begins differentiating
   within a single tick after cold start; subsequent samples blend with the
   prior smoothed value. At startup with no ORCA data every locality
   defaults to utilization 0 (full headroom), so the stage-4 local-preference
   check applies and routing snaps to the local locality (minus the probe
   floor) until the first reports arrive. See
   :ref:`Caveats <load_aware_locality_caveats>` for cold-start implications.
3. **Headroom weight.** Compute each locality's base weight as
   ``host_count * (1 - smoothed_util)`` -- capacity-weighted headroom. Stale
   localities fall back to ``host_count`` so traffic keeps flowing without
   artificially boosting them.
4. **Local preference.** If the local locality's smoothed utilization is at
   most ``utilization_variance_threshold`` above the host-count-weighted
   remote average, snap to all-local routing. One-sided: if the local
   locality is less loaded than the remote localities, all-local routing
   always applies regardless of gap size.
5. **Probe floor.** Enforce ``remote_probe_fraction`` by taking a slice of
   local weight and redistributing it across remote localities in proportion
   to host count -- not headroom -- so all remotes are sampled fairly. The
   amount taken from local is capped at the local weight itself.

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

Pseudocode
""""""""""

The pseudocode below specifies the exact semantics of the five stages
above:

::

  # Per-tick smoothing factor (consistent settling regardless of tick rate)
  alpha = 1 - exp(-weight_update_period / smoothing_time_constant)

  # Per-host sample validity filter (excludes hosts that have never reported,
  # plus hosts whose last ORCA report is older than weight_expiration_period;
  # if expiration is disabled, all reporting hosts qualify)
  valid(h)         = has_reported(h) and
                     (now - last_report_time(h)) <= weight_expiration_period
  valid_hosts(L)   = { h in hosts(L) : valid(h) }

  # Per-locality utilization (EWMA smoothed; first sample applied raw so
  # the policy reacts within one tick instead of waiting ~5 time constants
  # to converge from the cold-start prior of 0)
  if valid_hosts(L) is empty:
      if prev_smoothed_util(L) exists:
          smoothed_util(L) = prev_smoothed_util(L)   # carry prior value
          stale(L) = true
      else:                                          # never sampled (cold start)
          smoothed_util(L) = 0
          stale(L) = false
  else:
      raw_util(L) = avg over h in valid_hosts(L) of util(h)
      if no prior smoothed_util(L):              # first sample for L
          smoothed_util(L) = raw_util(L)
      else:
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
  total_host_count   = sum(host_count(L_i) for all L_i)
  remote_host_count  = sum(host_count(R_i) for all remote R_i)

  # All-overloaded fallback (skipped when the subset has no hosts at all)
  if total_base_weight == 0 and total_host_count > 0:
      adjusted_weight(L_i) = host_count(L_i)
  elif total_base_weight > 0:
      adjusted_weight(L_i) = base_weight(L_i)

      if local exists and remote_host_count > 0:
          # Local preference (one-sided: local must not be too far ABOVE remote;
          # skipped when local has no eligible hosts)
          remote_weighted_avg = sum(smoothed_util(R_i) * host_count(R_i))
                                / remote_host_count
          if host_count(local) > 0 and
             smoothed_util(local) <= remote_weighted_avg + utilization_variance_threshold:
              adjusted_weight(local) = total_base_weight
              adjusted_weight(R_i) = 0


          # Remote probe enforcement. Conserve total weight: take only as
          # much from local as it actually has, and redistribute exactly
          # that amount across remotes.
          total_adjusted_weight = sum(adjusted_weight(L_i) for all L_i)
          remote_weight = sum(adjusted_weight(R_i) for all remote R_i)
          remote_share = remote_weight / total_adjusted_weight
          if remote_share < remote_probe_fraction:
              deficit          = remote_probe_fraction * total_adjusted_weight - remote_weight
              take_from_local  = min(deficit, adjusted_weight(local))
              adjusted_weight(local) -= take_from_local
              for each remote R_i:
                  adjusted_weight(R_i) += take_from_local * host_count(R_i) / remote_host_count

  routing_share(L) = adjusted_weight(L) / sum(adjusted_weight(L_i) for all L_i)

Host-count proportional probe redistribution is intentional: when probing
for fresh data, the policy samples remote localities fairly rather than
biasing toward localities whose current (possibly stale) utilization
happens to look lower.

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
       Any LB policy may be configured here, but hash-based policies (ring
       hash, Maglev) build their structures cluster-wide and are not
       constrained by the locality pick. See
       :ref:`Caveats <load_aware_locality_caveats>`.
   * - ``weight_update_period``
     - 1 s
     - How often locality weights are recomputed from ORCA data. Must be at
       least 100 ms.
   * - ``metric_names_for_computing_utilization``
     - (unset)
     - Named ORCA metrics used to compute utilization. By default the max of
       matching values takes precedence over ``application_utilization`` when
       that max is greater than 0. Map entries use ``<map_field_name>.<map_key>``
       (e.g. ``named_metrics.foo``). See
       :ref:`Weight computation <load_aware_locality_weight_computation>` for
       precedence.
   * - ``utilization_variance_threshold``
     - 0.1
     - When the local locality's utilization exceeds the host-count-weighted
       remote average by no more than this threshold, all traffic routes
       locally. One-sided check: if the local locality is less loaded than
       the remote localities, all-local routing always applies. Range:
       [0, 1].
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
       proportionally to host count. Set to 0 to disable (safe only when
       cross-zone traffic must be strictly avoided). Range: [0, 1). See
       :ref:`Caveats <load_aware_locality_caveats>` for scaling notes.
   * - ``weight_expiration_period``
     - 3 minutes
     - Per-host sample validity window. Hosts that have not reported within
       this duration are excluded from their locality's utilization
       aggregation. The locality's EWMA continues over the remaining
       reporting hosts; if every host in a locality is stale, the locality
       falls back to host-count-proportional weighting. Tune higher to
       tolerate longer reporting gaps; tune lower to prune draining
       backends faster. Set to 0 s to disable expiration.

Priority support
^^^^^^^^^^^^^^^^

The policy respects Envoy's
:ref:`priority levels <arch_overview_load_balancing_priority_levels>`.
Priority selection happens first via the standard healthy/degraded
priority load calculation; locality selection then applies within the
chosen priority. Unlike zone-aware routing (priority 0 only), this
policy applies at all priority levels.

The policy honors ``common_lb_config.healthy_panic_threshold``. When
in panic mode the policy routes to all hosts.

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
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Out-of-band ORCA reporting is not yet supported.** Only in-band
  (per-response) ORCA reports are consumed. ``enable_oob_load_report`` and
  ``oob_reporting_period`` are accepted but currently have no effect.
- **Probing is required.** A locality only produces fresh ORCA samples
  when it receives traffic, so ``remote_probe_fraction`` must stay above 0
  to keep remote localities reporting. Set it to 0 only when cross-zone
  traffic must be strictly avoided.
- **Cold start snaps local.** Until the first ORCA reports arrive, every
  locality reads as utilization 0, so the stage-4 local-preference check
  routes ~100% of traffic to the local locality (minus
  ``remote_probe_fraction``) rather than a host-count split. Convergence is
  fast -- local hosts receive traffic immediately and report within about one
  tick -- but a local locality much smaller than its share of cold-start
  traffic can be briefly overloaded after a mass restart. Backends that never
  report ORCA leave the policy in this all-local mode permanently: the policy
  assumes ORCA-reporting backends.
- **Hash-based child policies.** Ring hash and Maglev build their hash
  structures once over the full cluster host set and select from those
  structures directly, ignoring the per-locality host slice this policy hands
  them. The locality pick therefore does not constrain them: locality
  weighting (local preference, variance threshold, ``remote_probe_fraction``)
  has no effect on traffic, and the zone-routing stats record locality
  decisions that traffic does not follow. The resulting behavior is that of
  the plain hash policy. Support for per-locality hash structures may be
  added in the future. Endpoint-selection retry is owned by the child LB.
- **Asynchronous child policies.** Child policies that select hosts
  asynchronously (for example dynamic modules or dynamic forward proxy) are
  not supported: this policy wraps the request context in a short-lived
  per-pick object, so an asynchronous selection is cancelled and the pick
  fails synchronously rather than calling back after the context is gone.
  Use a synchronous child policy.
- **Probe-fraction scaling.** ``remote_probe_fraction`` is a global value
  divided across all remote localities, then again across each locality's
  hosts. The per-host probe rate is therefore approximately
  ``Total RPS * remote_probe_fraction / (N remotes * Hosts/locality)``,
  and the expected interval between consecutive probes to a given host
  is the reciprocal. When that interval exceeds
  ``weight_expiration_period``, hosts are likely to go stale between
  probes and the locality falls back to host-count weighting -- defeating
  the load-awareness this policy provides.

  Approximate sample intervals per host at the default ``remote_probe_fraction``
  of 0.03:

  +-----------+-----------+----------------+----------------------+
  | Total RPS | N remotes | Hosts/locality | Sample interval/host |
  +===========+===========+================+======================+
  | 1000      | 3         | 10             | ~1 s                 |
  +-----------+-----------+----------------+----------------------+
  | 1000      | 100       | 10             | ~33 s                |
  +-----------+-----------+----------------+----------------------+
  | 100       | 100       | 10             | ~5.5 min             |
  +-----------+-----------+----------------+----------------------+

  The top row is comfortably faster than the 3-minute default expiration.
  The middle row is still safe but leaves less headroom. In the bottom
  row, samples expire before the next probe arrives, so remote
  localities will alternate between fresh data and host-count fallback
  every few ticks. To avoid this, either reduce locality count, raise
  ``remote_probe_fraction``, or raise ``weight_expiration_period`` to
  tolerate longer gaps.
- **Variance-threshold oscillation.** Workloads sitting near the
  ``utilization_variance_threshold`` boundary can theoretically oscillate
  between snap-to-local and spillover modes across consecutive ticks.
  EWMA smoothing dampens this in practice; tune
  ``smoothing_time_constant`` higher if oscillation is observed.
- **Child LB health view.** The child endpoint-picking LB sees only the
  hosts in its chosen locality, all marked healthy (a flattened health
  view). This is benign for ORCA-weighting children like CSWRR but
  means a child that relies on Envoy health flags will not see degraded
  or unhealthy markers within the locality slice.
- **Subsetting.** Load balancer
  :ref:`subsetting <arch_overview_load_balancer_subsets>` partitions
  hosts orthogonally to locality boundaries. The policy will operate
  over the post-subset host slice; per-locality weights are computed
  over whatever hosts remain after subsetting filters them. This is
  rarely the behavior subset users expect.

Statistics
^^^^^^^^^^

The policy emits stats under ``cluster.<cluster_name>.load_aware_locality.*``:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Counter
     - Increments when
   * - ``recompute_total``
     - Every main-thread weight-recompute tick. A liveness signal: the
       expected rate is one per ``weight_update_period``.
   * - ``all_overloaded_total``
     - Per tick where every locality's headroom was 0 (fallback to
       host-count weighting).
   * - ``local_preferred_total``
     - Per tick where the variance-threshold check snapped routing to
       100% local.
   * - ``probe_active_total``
     - Per tick where ``remote_probe_fraction`` redistribution kicked in.
   * - ``spill_active_total``
     - Per tick where local utilization exceeded the remote average by more
       than the variance threshold, spilling traffic to remote localities.
   * - ``stale_locality_total``
     - Incremented once per stale locality per priority per tick: a locality
       that previously had fresh reports but whose hosts' samples have all
       expired (fell back to host-count baseline). A locality stale in
       multiple priorities is counted once for each. Localities that have
       never reported -- e.g. at cold start -- are not counted. A
       single-priority 5-locality cluster with 2 stale localities adds 2
       each tick.

The four condition counters increment at most once per tick, ORed across
every priority and each of the three host subsets (healthy, degraded,
all-hosts) the policy weighs independently; ``stale_locality_total`` is
derived from the all-hosts subset.

Migrating from zone-aware routing? The per-request zone routing counters
(``lb_zone_routing_all_directly``, ``lb_zone_routing_sampled``,
``lb_zone_routing_cross_zone``) are still emitted with equivalent semantics,
recorded for the locality this policy selects. They are not incremented
while the priority is in panic mode.
``lb_recalculate_zone_structures`` is still emitted at cluster scope when
per-locality routing structures are rebuilt on membership changes (as in
zone-aware routing), not per weight-update tick. The per-tick counter closest
to ``lb_zone_routing_all_directly`` is
``load_aware_locality.local_preferred_total``.
