.. _config_cluster_manager_v1:

Cluster manager
===============

.. toctree::
  :hidden:

  cluster
  outlier
  cds
  sds

Cluster manager :ref:`architecture overview <arch_overview_cluster_manager>`.

.. code-block:: json

  {
    "clusters": [],
    "sds": "{...}",
    "local_cluster_name": "...",
    "outlier_detection": "{...}",
    "cds": "{...}"
  }

.. _config_cluster_manager_clusters:

:ref:`clusters <config_cluster_manager_cluster>`
  *(required, array)* A list of upstream clusters that the cluster manager performs
  :ref:`service discovery <arch_overview_service_discovery>`,
  :ref:`health checking <arch_overview_health_checking>`, and
  :ref:`load balancing <arch_overview_load_balancing>` on.

:ref:`sds <config_cluster_manager_sds>`
  *(sometimes required, object)* If any defined clusters use the :ref:`sds
  <config_cluster_manager_sds>` cluster type, a global SDS configuration must be specified.

.. _config_cluster_manager_local_cluster_name:

local_cluster_name
  *(optional, string)* Name of the local cluster (i.e., the cluster that owns the Envoy running this
  configuration). In order to enable
  :ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>` this option must be
  set. If *local_cluster_name* is defined then :ref:`clusters <config_cluster_manager_clusters>`
  must contain a definition of a cluster with the same name.

:ref:`outlier_detection <config_cluster_manager_outlier_detection>`
  *(optional, object)* Optional global configuration for outlier detection.

:ref:`cds <config_cluster_manager_cds_v1>`
  *(optional, object)* Optional configuration for the cluster discovery service (CDS) API.
