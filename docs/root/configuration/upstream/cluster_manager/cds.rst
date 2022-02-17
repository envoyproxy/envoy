.. _config_cluster_manager_cds:

Cluster discovery service (CDS)
===============================

The cluster discovery service (CDS) is an optional API that Envoy will call to dynamically fetch
cluster manager members. Envoy will reconcile the API response and add, modify, or remove known
clusters depending on what is required.

.. note::

  Any clusters that are statically defined within the Envoy configuration cannot be modified or
  removed via the CDS API.

* :ref:`v3 CDS API <v3_grpc_streaming_endpoints>`

Statistics
----------

CDS has a :ref:`statistics <subscription_statistics>` tree rooted at *cluster_manager.cds.*
