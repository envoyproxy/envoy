.. _config_listeners_lds:

Listener discovery service
==========================

The listener discovery service (LDS) is an optional API that Envoy will call to dynamically fetch
listeners. Envoy will reconcile the API response and add, modify, or remove known listeners
depending on what is required.

The semantics of listener updates are as follows:

* Every listener must have a unique :ref:`name <config_listeners_name>`. If a name is not
  provided, Envoy will create a UUID. Listeners that are to be dynamically updated should have a
  unique name supplied by the management server.
* When a listener is added, it will be "warmed" before taking traffic. For example, if the listener
  references an :ref:`RDS <config_http_conn_man_rds>` configuration, that configuration will be
  resolved and fetched before the listener is moved to "active."
* Listeners are effectively constant once created. Thus, when a listener is updated, an entirely
  new listener is created (with the same listen socket). This listener goes through the same
  warming process described above for a newly added listener.
* When a listener is updated or removed, the old listener will be placed into a "draining" state
  much like when the entire server is drained for restart. Connections owned by the listener will
  be gracefully closed (if possible) for some period of time before the listener is removed and any
  remaining connections are closed. The drain time is set via the :option:`--drain-time-s` option.

.. code-block:: json

  {
    "cluster": "...",
    "refresh_delay_ms": "..."
  }

cluster
  *(required, string)* The name of an upstream :ref:`cluster <config_cluster_manager_cluster>` that
  hosts the listener discovery service. The cluster must run a REST service that implements the
  :ref:`LDS HTTP API <config_listeners_lds_api>`. NOTE: This is the *name* of a cluster defined
  in the :ref:`cluster manager <config_cluster_manager>` configuration, not the full definition of
  a cluster as in the case of SDS and CDS.

refresh_delay_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the LDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_delay_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_delay_ms*. Default value
  is 30000ms (30 seconds).

.. _config_listeners_lds_api:

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

Statistics
----------

LDS has a statistics tree rooted at *listener_manager.lds.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed (either network or schema errors)
  version, Gauge, Hash of the contents from the last successful API fetch
