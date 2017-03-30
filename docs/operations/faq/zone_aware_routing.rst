.. _common_configuration_zone_aware_routing:

Zone aware routing
==================

There are several steps required for enabling :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>`
between source service A and destination service B.

Envoy configuration on source service
-------------------------------------
* :ref:`sds <config_cluster_manager_type>` type must be used for cluster B definition.
  See more on service discovery :ref:`here <arch_overview_service_discovery_sds>`.
* sds type must be used for cluster A definition.
* :ref:`local_cluster_name <config_cluster_manager_local_cluster_name>` must be set to cluster A.
  Only essential part is listed in the configuration below.

.. code-block:: json

  {
    "sds": "{...}",
    "clusters": [
      {
        "name": "cluster_a",
        "type": "sds",
      },
      {
        "name": "cluster_b",
        "type": "sds"
      }
    ],
    "local_cluster_name": "cluster_a"
  }

* Envoy must be launched with :option:`--service-zone` option which defines availability zone for current host.

Envoy configuration on destination service
------------------------------------------
* All hosts from cluster B should report `zone data <https://github.com/lyft/discovery#tags-json>`_
  to `discovery service <https://github.com/lyft/discovery#post-v1registrationservice>`_ as part of the registry process.

Verify it works
---------------
* Use :ref:`per zone <config_cluster_manager_cluster_per_az_stats>` Envoy stats to monitor cross zone traffic.

The above configuration is necessary for zone aware routing, but there are certain conditions
when zone aware routing is not performed, see details
:ref:`here <arch_overview_load_balancing_zone_aware_routing_preconditions>`.
