.. _arch_overview_load_balancing_panic_threshold:

Panic threshold
---------------

During load balancing, Envoy will generally only consider available (healthy or degraded) hosts in
an upstream cluster. However, if the percentage of available hosts in the cluster becomes too low,
Envoy will disregard health status and balance amongst all hosts. This is known as the *panic
threshold*. The default panic threshold is 50%. This is
:ref:`configurable <config_cluster_manager_cluster_runtime>` via runtime as well as in the
:ref:`cluster configuration <envoy_api_field_Cluster.CommonLbConfig.healthy_panic_threshold>`.
The panic threshold is used to avoid a situation in which host failures cascade throughout the
cluster as load increases.

Panic thresholds work in conjunction with priorities. If the number of available hosts in a given
priority goes down, Envoy will try to shift some traffic to lower priorities. If it succeeds in
finding enough available hosts in lower priorities, Envoy will disregard panic thresholds. In
mathematical terms, if normalized total availability across all priority levels is 100%, Envoy
disregards panic thresholds and continues to distribute traffic load across priorities according to
the algorithm described :ref:`here <arch_overview_load_balancing_priority_levels>`.
However, when normalized total availability drops below 100%, Envoy assumes that there are not enough
available hosts across all priority levels. It continues to distribute traffic load across priorities,
but if a given priority level's availability is below the panic threshold, traffic will go to all hosts
in that priority level regardless of their availability.

The following examples explain the relationship between normalized total availability and panic threshold.
It is assumed that the default value of 50% is used for the panic threshold.

Assume a simple set-up with 2 priority levels, P=1 100% healthy. In this scenario normalized total
health is always 100%, P=0 never enters panic mode, and Envoy is able to shift as much traffic as
necessary to P=1.

+-------------+------------+--------------+------------+--------------+--------------+
| P=0 healthy | Traffic    | P=0 in panic | Traffic    | P=1 in panic | normalized   |
| endpoints   |  to P=0    |              | to P=1     |              | total health |
+=============+============+==============+============+==============+==============+
| 72%         |  100%      | NO           |    0%      | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 71%         |   99%      | NO           |    1%      | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 50%         |   70%      | NO           |   30%      | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 25%         |   35%      | NO           |   65%      | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 0%          |    0%      | NO           |  100%      | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+

If P=1 becomes unhealthy, panic threshold continues to be disregarded until the sum of the health
P=0 + P=1 goes below 100%. At this point Envoy starts checking panic threshold value for each
priority.

+-------------+-------------+----------+--------------+----------+--------------+-------------+
| P=0 healthy | P=1 healthy | Traffic  | P=0 in panic | Traffic  | P=1 in panic | normalized  |
| endpoints   | endpoints   | to P=0   |              | to P=1   |              | total health|
+=============+=============+==========+==============+==========+==============+=============+
| 72%         |  72%        |  100%    | NO           |   0%     | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 71%         |  71%        |  99%     | NO           |   1%     | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 50%         |  60%        |  70%     | NO           |   30%    | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 25%         |  100%       |  35%     | NO           |   65%    | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 25%         |  25%        |  50%     | YES          |   50%    | YES          |  70%        |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 5%          |  65%        |  7%      | YES          |   93%    | NO           |  98%        |
+-------------+-------------+----------+--------------+----------+--------------+-------------+

Note that panic thresholds can be configured *per-priority*.

