.. _config_cluster_manager_sds:

Service discovery service
=========================

Service discovery service architecture overview.

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

.. _config_cluster_manager_sds_api:

REST API
--------

Envoy expects the service discovery service to expose the following API (See Lyft's
`reference implementation <https://github.com/lyft/discovery>`_):

.. http:get:: /v1/registration/(string: service_name)

  Asks the discovery service to return all hosts for a particular `service_name`. `service_name`
  corresponds to the :ref:`service_name <config_cluster_manager_cluster_service_name>` cluster
  parameter. Responses use the following JSON schema:

  .. code-block:: json

    {
      "hosts": []
    }

  hosts
    *(Required, array)* A list of :ref:`hosts <config_cluster_manager_sds_api_host>` that make up
    the service.

.. _config_cluster_manager_sds_api_host:

Host JSON
---------

.. code-block:: json

  {
    "ip_address": "...",
    "port": "...",
    "tags": {
      "az": "...",
      "canary": "...",
      "load_balancing_weight": "..."
    }
  }

ip_address
  *(required, string)* The IP address of the upstream host.

port
  *(required, integer)* The port of the upstream host.

.. _config_cluster_manager_sds_api_host_az:

az
  *(optional, string)* The optional zone of the upstream host. Envoy uses the zone for various
  statistics and load balancing tasks documented elsewhere.

canary
  *(optional, boolean)* The optional canary status of the upstream host. Envoy uses the canary
  status for various statistics and load balancing tasks documented elsewhere.

load_balancing_weight
  *(optional, integer)* The optional load balancing weight of the upstream host, in the range
  1 - 100. Envoy uses the load balancing weight in some of the built in load balancers.
