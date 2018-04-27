.. _config_cluster_manager_cds:

Cluster discovery service
=========================

The cluster discovery service (CDS) is an optional API that Envoy will call to dynamically fetch
cluster manager members. Envoy will reconcile the API response and add, modify, or remove known
clusters depending on what is required.

.. note::

  Any clusters that are statically defined within the Envoy configuration cannot be modified or
  removed via the CDS API.

* :ref:`v1 CDS API <config_cluster_manager_cds_v1>`
* :ref:`v2 CDS API <v2_grpc_streaming_endpoints>`

Statistics
----------

CDS has a statistics tree rooted at *cluster_manager.cds.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed because of schema errors
  version, Gauge, Hash of the contents from the last successful API fetch
