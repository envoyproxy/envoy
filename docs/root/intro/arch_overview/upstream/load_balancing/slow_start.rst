.. _arch_overview_load_balancing_slow_start:

Slow start mode
===============

Slow start mode is a configuration setting in Envoy to progressively increase amount of traffic for newly spawned service instances.
With no slow start enabled Envoy would send a proportional amount of traffic to new instances.
This could be undesirable for services that require warm up time to serve full production load and could result in request timeouts, loss of data and deteriorated user experience.

Slow start mode is a weight-adjustment mechanism in number of load balancer types and can be configured per cluster basis. 
Currently, slow start is supported in Round Robin and Least Request load balancer types.

Users can specify a slow start window parameter (in seconds), so that if host’s “cluster membership duration" (amount of time since it has joined the cluster) is within the configured window, it enters slow start mode, given that the host satisfies endpoint warming policy. 
Whenever a slow start window duration elapses, it exits slow start mode and gets regular amount of traffic acccording to load balanacing algorithm.
Host could also exit slow start mode in case it leaves the cluster.

To reiterate, host enters slow start mode when:
  * For :ref:`NO_WAIT<envoy_v3_api_enum_value_config.cluster.v3.Cluster.CommonLbConfig.EndpointWarmingPolicy.NO_WAIT>` endpoint warming policy, immediately if it's cluster membership duration is within slow start window.
  * For :ref:`WAIT_FOR_FIRST_PASSING_HC<envoy_v3_api_enum_value_config.cluster.v3.Cluster.CommonLbConfig.EndpointWarmingPolicy.WAIT_FOR_FIRST_PASSING_HC>` endpoint warming policy, upon passing an active healthcheck, given that it's cluster membership duration is within slow start window.

Host exists slow start mode when:
  * It leaves the cluster.
  * It's cluster membership duration is greater than slow start window.
  * For :ref:`WAIT_FOR_FIRST_PASSING_HC<envoy_v3_api_enum_value_config.cluster.v3.Cluster.CommonLbConfig.EndpointWarmingPolicy.WAIT_FOR_FIRST_PASSING_HC>` endpoint warming policy, if host's health status changes to anything rather than `Healthy`.

Below is example of how requests would be distributed across hosts with Round Robin Loadbalancer, slow start window of 10 seconds, `NO_WAIT` ednpoint warming policy and 0.5 time bias.
Host H1 has statically configured initial weight of X and host H2 weight of Y, the actual numerical values are of no significance for this example.

+-------------+----------------+------------+------------+-----------+----------+-------------+
| Timestamp   | Event          | H1 in slow | H2 in slow | H1 LB     | H2 LB    | LB decision |
|             |                | start      | start      | weight    | weight   |             |
+=============+================+============+============+===========+==========+=============+
| 1           |  H1 create     |    YES     |     --     |   0.5X    |    --    |     --      |
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 11          |  H2 create     |     NO     |    YES     |    X      |   0.5Y   |     --      |
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 12          | LB select host |     NO     |    YES     |    X      |   0.5Y   |     H1      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 13          | LB select host |     NO     |    YES     |    X      |   0.5Y   |     H1      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 14          | LB select host |     NO     |    YES     |    X      |   0.5Y   |     H1      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 15          |LB select host  |     NO     |    YES     |    X      |   0.5Y   |     H2      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 22          | LB select host |     NO     |     NO     |    X      |    Y     |     H1      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+
| 23          | LB select host |     NO     |     NO     |    X      |    Y     |     H2      | 
+-------------+----------------+------------+------------+-----------+----------+-------------+


.. _arch_overview_load_balancing_slow_start_endpoint_warming_policy_types:

Endpoint warming policy types
-----------------------------

Endpoint warming policy defines conditions for host to enter slow start mode.

No Wait
^^^^^^^

If configured, host would enter slow start immediately.

Wait For First Passing Healthcheck
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If configured, host would enter slow start upon having passed an active healthcheck.