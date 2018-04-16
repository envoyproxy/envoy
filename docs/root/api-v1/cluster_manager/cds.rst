.. _config_cluster_manager_cds_v1:

Cluster discovery service
=========================

.. code-block:: json

  {
    "cluster": "{...}",
    "refresh_delay_ms": "..."
  }

:ref:`cluster <config_cluster_manager_cluster>`
  *(required, object)* A standard definition of an upstream cluster that hosts the cluster
  discovery service. The cluster must run a REST service that implements the :ref:`CDS HTTP API
  <config_cluster_manager_cds_api>`.

refresh_delay_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the CDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_delay_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_delay_ms*. Default value
  is 30000ms (30 seconds).

.. _config_cluster_manager_cds_api:

REST API
--------

.. http:get:: /v1/clusters/(string: service_cluster)/(string: service_node)

Asks the discovery service to return all clusters for a particular `service_cluster` and
`service_node`. `service_cluster` corresponds to the :option:`--service-cluster` CLI option.
`service_node` corresponds to the :option:`--service-node` CLI option. Responses use the following
JSON schema:

.. code-block:: json

  {
    "clusters": []
  }

clusters
  *(Required, array)* A list of :ref:`clusters <config_cluster_manager_cluster>` that will be
  dynamically added/modified within the cluster manager. Envoy will reconcile this list with the
  clusters that are currently loaded and either add/modify/remove clusters as necessary.
