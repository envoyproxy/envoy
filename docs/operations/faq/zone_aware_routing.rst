.. _common_configuration_zone_aware_routing:

Zone aware routing
==================

There are several steps required for enabling :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>`
between source service A and destination service B. See below for specific configuration/setup on source
and destination service/cluster.

Envoy configuration on source service
-------------------------------------
This section describes specific configuration for Envoy running side by side with the service A.
These are the requirements:

* Envoy must be launched with :option:`--service-zone` option which defines availability zone for the current host.
* Both cluster A and cluster B must have :ref:`sds <config_cluster_manager_type>` type.
  See more on service discovery :ref:`here <arch_overview_service_discovery_sds>`.
* :ref:`local_cluster_name <config_cluster_manager_local_cluster_name>` must be set to cluster A.
  Only essential parts are listed in the configuration below.

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

Envoy configuration on destination service
------------------------------------------
It's not necessary to run Envoy side by side with service B, but it's important that each host
in cluster B registers with the discovery service queried by Envoy routing from service A.
Specifically you need to setup a periodic process to register hosts from service B with the
`discovery service <https://github.com/lyft/discovery#post-v1registrationservice>`_.
And each registration call must have `zone data <https://github.com/lyft/discovery#tags-json>`_
provided.

Verify it works
---------------
* Use :ref:`per zone <config_cluster_manager_cluster_per_az_stats>` Envoy stats to monitor cross zone traffic.

The above configuration is necessary for zone aware routing, but there are certain conditions
when zone aware routing is not performed, see details
:ref:`here <arch_overview_load_balancing_zone_aware_routing_preconditions>`.
