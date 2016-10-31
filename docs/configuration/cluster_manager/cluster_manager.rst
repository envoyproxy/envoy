.. _config_cluster_manager:

Cluster manager
===============

Cluster manager :ref:`architecture overview <arch_overview_cluster_manager>`.

.. code-block:: json

  {
    "clusters": [],
    "sds": "{...}",
    "local_cluster_name": "..."
  }

.. _config_cluster_manager_clusters:

:ref:`clusters <config_cluster_manager_cluster>`
  *(required, array)* A list of upstream clusters that the cluster manager performs
  :ref:`service discovery <arch_overview_service_discovery>`,
  :ref:`health checking <arch_overview_health_checking>`, and
  :ref:`load balancing <arch_overview_load_balancing>` on.

:ref:`sds <config_cluster_manager_sds>`
  *(sometimes required, object)* If any defined clusters use the :ref:`sds
  <arch_overview_service_discovery_sds>` cluster type, a global SDS configuration must be specified.

local_cluster_name
  *(optional, string)* Name of the local cluster. In order to enable
  :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>` this option must be set.
  If local_cluster_name is defined then :ref:`clusters <config_cluster_manager_clusters>`
  must contain definition of local cluster.

.. toctree::
  :hidden:

  cluster
  sds
  sds_api
