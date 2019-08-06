.. _subscription_statistics:

xDS subscription statistics
===========================

Envoy discovers its various dynamic resources via discovery
services referred to as *xDS*. Resources are requested via :ref:`subscriptions <xds_protocol>`, 
by specifying a filesystem path to watch, initiating gRPC streams or polling a REST-JSON URL. 

The following statistics are generated for all subscriptions.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  init_fetch_timeout, Counter, Total :ref:`initial fetch timeouts <envoy_api_field_core.ConfigSource.initial_fetch_timeout>`
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed because of network errors
  update_rejected, Counter, Total API fetches that failed because of schema/validation errors
  version, Gauge, Hash of the contents from the last successful API fetch
  control_plane.connected_state, Gauge, A boolean (1 for connected and 0 for disconnected) that indicates the current connection state with management server
