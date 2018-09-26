.. _config_listeners_lds:

Listener discovery service (LDS)
================================

The listener discovery service (LDS) is an optional API that Envoy will call to dynamically fetch
listeners. Envoy will reconcile the API response and add, modify, or remove known listeners
depending on what is required.

The semantics of listener updates are as follows:

* Every listener must have a unique :ref:`name <envoy_api_field_Listener.name>`. If a name is not
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

  .. note::

    Any listeners that are statically defined within the Envoy configuration cannot be modified or
    removed via the LDS API.

Configuration
-------------

* :ref:`v1 LDS API <config_listeners_lds_v1>`
* :ref:`v2 LDS API <v2_grpc_streaming_endpoints>`

Statistics
----------

LDS has a statistics tree rooted at *listener_manager.lds.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed because of network errors
  update_rejected, Counter, Total API fetches that failed because of schema/validation errors
  version, Gauge, Hash of the contents from the last successful API fetch
  control_plane.connected_state, Gauge, A boolan (1 for connected and 0 for disconnected) that indicates the current connection state with management server