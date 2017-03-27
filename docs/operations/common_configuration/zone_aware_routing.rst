.. _common_configuration_zone_aware_routing:

Zone aware routing
==================

Do the following if you want to enable :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>`
between cluster A and cluster B:

* Both cluster A and cluster B must be using :ref:`discovery service <arch_overview_service_discovery_sds>` with
  zone information provided.
* Envoy must be launched with :option:`--service-zone` option on cluster A.
* :ref:`local_cluster_name <config_cluster_manager_local_cluster_name>` must be defined for cluster A configuration.
* See other preconditions on cluster size, runtime values :ref:`here <arch_overview_load_balancing_zone_aware_routing>`.
