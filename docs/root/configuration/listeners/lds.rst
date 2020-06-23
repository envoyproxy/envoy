.. _config_listeners_lds:

Listener discovery service (LDS)
================================

The listener discovery service (LDS) is an optional API that Envoy will call to dynamically fetch
listeners. Envoy will reconcile the API response and add, modify, or remove known listeners
depending on what is required.

The semantics of listener updates are as follows:

* Every listener must have a unique :ref:`name <envoy_v3_api_field_config.listener.v3.Listener.name>`. If a name is not
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

* :ref:`v3 LDS API <v2_grpc_streaming_endpoints>`

Statistics
----------

LDS has a :ref:`statistics <subscription_statistics>` tree rooted at *listener_manager.lds.*
