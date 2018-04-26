.. _config_listeners_lds_v1:

Listener discovery service (LDS)
================================

.. code-block:: json

  {
    "cluster": "...",
    "refresh_delay_ms": "..."
  }

cluster
  *(required, string)* The name of an upstream :ref:`cluster <config_cluster_manager_cluster>` that
  hosts the listener discovery service. The cluster must run a REST service that implements the
  :ref:`LDS HTTP API <config_listeners_lds_v1_api>`. NOTE: This is the *name* of a statically defined
  cluster in the :ref:`cluster manager <config_cluster_manager>` configuration, not the full definition of
  a cluster as in the case of SDS and CDS.

refresh_delay_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the LDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_delay_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_delay_ms*. Default value
  is 30000ms (30 seconds).

.. _config_listeners_lds_v1_api:

REST API
--------

.. http:get:: /v1/listeners/(string: service_cluster)/(string: service_node)

Asks the discovery service to return all listeners for a particular `service_cluster` and
`service_node`. `service_cluster` corresponds to the :option:`--service-cluster` CLI option.
`service_node` corresponds to the :option:`--service-node` CLI option. Responses use the following
JSON schema:

.. code-block:: json

  {
    "listeners": []
  }

listeners
  *(Required, array)* A list of :ref:`listeners <config_listeners>` that will be
  dynamically added/modified within the listener manager. The management server is expected to
  respond with the complete set of listeners that Envoy should configure during each polling cycle.
  Envoy will reconcile this list with the listeners that are currently loaded and either
  add/modify/remove listeners as necessary.
