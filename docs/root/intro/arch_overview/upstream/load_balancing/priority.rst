.. _arch_overview_load_balancing_priority_levels:

Priority levels
------------------

During load balancing, Envoy will generally only consider hosts configured at the highest priority
level. For each EDS :ref:`LocalityLbEndpoints<envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` an optional
priority may also be specified. When endpoints at the highest priority level (P=0) are healthy, all
traffic will land on endpoints in that priority level. As endpoints for the highest priority level
become unhealthy, traffic will begin to trickle to lower priority levels.

The system can be overprovisioned with a configurable
:ref:`overprovisioning factor <arch_overview_load_balancing_overprovisioning_factor>`, which
currently defaults to 1.4 (this document will assume this value). If 80% of the endpoints in a
priority level are healthy, that level is still considered fully healthy because 80*1.4 > 100.
So, level 0 endpoints will continue to receive all traffic until less than ~71.4% of them are
healthy.

The priority level logic works with integer health scores. The health score of a level is
(percent of healthy hosts in the level) * (overprovisioning factor), capped at 100%. P=0
endpoints receive (level 0's health score) percent of the traffic, with the rest flowing
to P=1 (assuming P=1 is 100% healthy - more on that later). For instance, when 50% of P=0
endpoints are healthy, they will receive 50 * 1.4 = 70% of the traffic.
The integer percents of traffic that each priority level receives are collectively called the
system's "priority load". More examples (with 2 priority levels, P=1 100% healthy):

+----------------------------+----------------+-----------------+
| P=0 healthy endpoints      | Traffic to P=0 |  Traffic to P=1 |
+============================+================+=================+
| 100%                       | 100%           |   0%            |
+----------------------------+----------------+-----------------+
| 72%                        | 100%           |   0%            |
+----------------------------+----------------+-----------------+
| 71%                        | 99%            |   1%            |
+----------------------------+----------------+-----------------+
| 50%                        | 70%            |   30%           |
+----------------------------+----------------+-----------------+
| 25%                        | 35%            |   65%           |
+----------------------------+----------------+-----------------+
| 0%                         | 0%             |   100%          |
+----------------------------+----------------+-----------------+

.. attention::

  In order for the load distribution algorithm and normalized total health calculation to work
  properly, each priority level must be able to handle (100% * overprovision factor) of the
  traffic: Envoy assumes a 100% healthy P=1 can take over entirely for an unhealthy P=0, etc.
  If P=0 has 10 hosts but P=1 only has 2 hosts, that assumption probably will not hold.

The health score represents a level's current ability to handle traffic, after factoring in how
overprovisioned the level originally was, and how many endpoints are currently unhealthy.
Therefore, if the sum across all levels' health scores is < 100, then Envoy believes there are not
enough healthy endpoints to fully handle the traffic. This sum is called the "normalized total
health." When normalized total health drops below 100, traffic is distributed after normalizing
the levels' health scores to that sub-100 total. E.g. healths of {20, 30} (yielding a normalized
total health of 50) would be normalized, and result in a priority load of {40%, 60%} of traffic.

+------------------------+-------------------------+-----------------+----------------+
| P=0 healthy endpoints  | P=1 healthy endpoints   | Traffic to  P=0 | Traffic to P=1 |
+========================+=========================+=================+================+
| 100%                   |  100%                   | 100%            |   0%           |
+------------------------+-------------------------+-----------------+----------------+
| 72%                    |  72%                    | 100%            |   0%           |
+------------------------+-------------------------+-----------------+----------------+
| 71%                    |  71%                    | 99%             |   1%           |
+------------------------+-------------------------+-----------------+----------------+
| 50%                    |  50%                    | 70%             |   30%          |
+------------------------+-------------------------+-----------------+----------------+
| 25%                    |  100%                   | 35%             |   65%          |
+------------------------+-------------------------+-----------------+----------------+
| 25%                    |  25%                    | 50%             |   50%          |
+------------------------+-------------------------+-----------------+----------------+

As more priorities are added, each level consumes load equal to its normalized effective health,
unless the healths of the levels above it sum to 100%, in which case it receives no load.

+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| P=0 healthy endpoints | P=1 healthy endpoints | P=2 healthy endpoints | Traffic to P=0 | Traffic to P=1 | Traffic to P=2 |
+=======================+=======================+=======================+================+================+================+
| 100%                  |  100%                 |  100%                 | 100%           |   0%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 72%                   |  72%                  |  100%                 | 100%           |   0%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 71%                   |  71%                  |  100%                 | 99%            |   1%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 50%                   |  50%                  |  100%                 | 70%            |   30%          |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 25%                   |  100%                 |  100%                 | 35%            |   65%          |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 25%                   |  25%                  |  100%                 | 35%            |   35%          |   30%          |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 25%                   |  25%                  |   20%                 | 36%            |   36%          |   28%          |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+

To sum this up in pseudo algorithms:

::

  health(P_X) = min(100, 1.4 * 100 * healthy_P_X_backends / total_P_X_backends)
  normalized_total_health = min(100, Σ(health(P_0)...health(P_X)))
  priority_load(P_0) = min(100, health(P_0) * 100 / normalized_total_health)
  priority_load(P_X) = min(100 - Σ(priority_load(P_0)..priority_load(P_X-1)),
                           health(P_X) * 100 / normalized_total_health)

Note: This sectioned talked about healthy priorities, but this also extends to
:ref:`degraded priorities <arch_overview_load_balancing_degraded>`.

