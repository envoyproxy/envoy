.. _arch_overview_load_balancing_slow_start:

Slow start mode
===============

Slow start mode is a configuration setting in Envoy to progressively increase amount of traffic for newly added upstream endpoints.
With no slow start enabled Envoy would send a proportional amount of traffic to new upstream ednpoints.
This could be undesirable for services that require warm up time to serve full production load and could result in request timeouts, loss of data and deteriorated user experience.

Slow start mode is a mechanism that affects load balancing weight of upstream endpoints and can be configured per upstream cluster. 
Currently, slow start is supported in Round Robin and Least Request load balancer types.

Users can specify a :ref:`slow start window parameter<envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.SlowStartConfig.slow_start_window>` (in seconds), so that if endpoint “cluster membership duration" (amount of time since it has joined the cluster) is within the configured window, it enters slow start mode. 
During slow start window, load balancing weight of a particular endpoint will be scaled with :ref:`time bias parameter<envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.SlowStartConfig.time_bias>`, e.g.:
`weight = load_balancing_weight * time_bias * time_factor`.
Time factor is value that increases as time progresses, and is calculated like:
`time_factor = (1 / slow_start_window_seconds) * endpoint_create_duration_seconds`

The longer slow start window is the less traffic would be sent to endpoint as time advances within slow start window.

Whenever a slow start window duration elapses, upstream endpoint exits slow start mode and gets regular amount of traffic acccording to load balanacing algorithm.
Its load balancing weight will no longer be scaled with runtime bias. Endpoint could also exit slow start mode in case it leaves the cluster.

To reiterate, endpoint enters slow start mode when:
  * If no active healthcheck is configured per cluster, immediately if its cluster membership duration is within slow start window.
  * In case an active healthcheck is configured per cluster, when its cluster membership duration is within slow start window and endpoint has passed an active healthcheck. 
    If endpoint does not pass an active healcheck during entire slow start window (since it has been added to upstream cluster), then it never enters slow start mode.

Endpoint exits slow start mode when:
  * It leaves the cluster.
  * Its cluster membership duration is greater than slow start window.
  * It does not pass an active healcheck configured per cluster.
    Endpoint could further re-enter slow start, if it passes an active healtcheck and its creation time is within slow start window.

Below is example of how requests would be distributed across endpoints with Round Robin Loadbalancer, slow start window of 10 seconds, no active healcheck and 0.5 time bias.
Endpoint E1 has statically configured initial weight of X and endpoint E2 weight of Y, the actual numerical values are of no significance for this example.

+-------------+--------------------+------------+------------+-----------+----------+-------------+
| Timestamp   | Event              | E1 in slow | E2 in slow | E1 LB     | E2 LB    | LB decision |
|             |                    | start      | start      | weight    | weight   |             |
+=============+====================+============+============+===========+==========+=============+
| 1           |  E1 create         |    YES     |     --     |   0.5X    |    --    |     --      |
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 11          |  E2 create         |     NO     |    YES     |    X      |   0.5Y   |     --      |
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 12          | LB select endpoint |     NO     |    YES     |    X      |   0.5Y   |     E1      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 13          | LB select endpoint |     NO     |    YES     |    X      |   0.5Y   |     E1      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 14          | LB select endpoint |     NO     |    YES     |    X      |   0.5Y   |     E1      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 15          |LB select endpoint  |     NO     |    YES     |    X      |   0.5Y   |     E2      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 22          | LB select endpoint |     NO     |     NO     |    X      |    Y     |     E1      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+
| 23          | LB select endpoint |     NO     |     NO     |    X      |    Y     |     E2      | 
+-------------+--------------------+------------+------------+-----------+----------+-------------+