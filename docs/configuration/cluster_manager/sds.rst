.. _config_cluster_manager_sds:

Service discovery service
=========================

Service discovery service :ref:`architecture overview <arch_overview_service_discovery_sds>`.

.. code-block:: json

  {
    "cluster": "{...}",
    "refresh_delay_ms": "{...}"
  }

:ref:`cluster <config_cluster_manager_cluster>`
  *(required, object)* A standard definition of an upstream cluster that hosts the service
  discovery service. The cluster must run a REST service that implements the :ref:`SDS HTTP API
  <config_cluster_manager_sds_api>`.

refresh_delay_ms
  *(required, integer)* The delay, in milliseconds, between fetches to the SDS API for each
  configured SDS cluster. Envoy will add an additional random jitter to the delay that is between
  zero and *refresh_delay_ms* milliseconds. Thus the longest possible refresh delay is
  2 \* *refresh_delay_ms*.
