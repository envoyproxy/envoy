.. _config_cluster_manager_cds:

Cluster discovery service
=========================

The cluster discovery service (CDS) is an optional API that Envoy will call to dynamically fetch
cluster manager members. Envoy will reconcile the API response and add, modify, or remove known
clusters depending on what is required.

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
  clusters that are currently loaded and either add/modify/remove clusters as necessary. Note that
  any clusters that are statically defined within the Envoy configuration cannot be modified via
  the CDS API.

Statistics
----------

CDS has a statistics tree rooted at *cluster_manager.cds.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed (either network or schema errors)
  version, Gauge, Hash of the contents from the last successful API fetch
