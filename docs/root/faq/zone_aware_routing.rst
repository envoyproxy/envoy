.. _common_configuration_zone_aware_routing:

How do I setup zone aware routing?
==================================

There are several steps required for enabling :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>`
between source service ("cluster_a") and destination service ("cluster_b").

Envoy configuration on the source service
-----------------------------------------
This section describes the specific configuration for the Envoy running side by side with the source service.
These are the requirements:

* Envoy must be launched with :option:`--service-zone` option which defines the zone for the current host.
* Both definitions of the source and the destination clusters must have :ref:`EDS <envoy_api_field_Cluster.type>` type.
* :ref:`local_cluster_name <envoy_api_field_config.bootstrap.v2.ClusterManager.local_cluster_name>` must be set to the
  source cluster.

  Only essential parts are listed in the configuration below for the cluster manager.

.. code-block:: json

  {
    "sds": "{...}",
    "local_cluster_name": "cluster_a",
    "clusters": [
      {
        "name": "cluster_a",
        "type": "sds",
      },
      {
        "name": "cluster_b",
        "type": "sds"
      }
    ]
  }

Envoy configuration on the destination service
----------------------------------------------
It's not necessary to run Envoy side by side with the destination service, but it's important that each host in the
destination cluster registers with the discovery service :ref:`queried by the source service Envoy
<config_overview_v2_management_server>`. :ref:`Zone <envoy_api_msg_endpoint.LocalityLbEndpoints>`
information must be available as part of that response.

Only zone related data is listed in the response below.

.. code-block:: json

   {
      "tags": {
          "az": "us-east-1d"
      }
   }

Infrastructure setup
--------------------
The above configuration is necessary for zone aware routing, but there are certain conditions
when zone aware routing is :ref:`not performed <arch_overview_load_balancing_zone_aware_routing_preconditions>`.

Verification steps
------------------
* Use :ref:`per zone <config_cluster_manager_cluster_per_az_stats>` Envoy stats to monitor cross zone traffic.
