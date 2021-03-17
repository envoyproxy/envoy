.. _arch_overview_load_balancing_excluded:

Excluded endpoints
------------------

Certain conditions may cause Envoy to *exclude* endpoints from load balancing. Excluding a host
means that for any load balancing calculations that adjust weights based on the ratio of eligible
hosts and total hosts (priority spillover, locality weighting and panic mode) Envoy will exclude
these hosts in the denominator.

For example, with hosts in two priorities P0 and P1, where P0 looks like {healthy, unhealthy
(excluded), unhealthy (excluded)} and where P1 looks like {healthy, healthy} all traffic will still
hit P0, as 1 / (3 - 2) = 1.

Excluded hosts allow scaling up or down the number of hosts for a given cluster without entering
panic mode or triggering priority spillover.

If panic mode is triggered, excluded hosts are still eligible for traffic; they simply do not
contribute to the calculation when deciding whether panic mode is enabled or not.

Currently, the following two conditions can lead to a host being excluded when using active
health checking:

* Using the :ref:`ignore_new_hosts_until_first_hc
  <envoy_api_field_Cluster.CommonLbConfig.ignore_new_hosts_until_first_hc>` cluster option.
* Receiving the :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header in a normal routed
  response or in response to an :ref:`HTTP active health check
  <arch_overview_health_checking_fast_failure>`.