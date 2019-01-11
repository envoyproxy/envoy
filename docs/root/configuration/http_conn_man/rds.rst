.. _config_http_conn_man_rds:

Route discovery service (RDS)
=============================

The route discovery service (RDS) API is an optional API that Envoy will call to dynamically fetch
:ref:`route configurations <envoy_api_msg_RouteConfiguration>`. A route configuration includes both
HTTP header modifications, virtual hosts, and the individual route entries contained within each
virtual host. Each :ref:`HTTP connection manager filter <config_http_conn_man>` can independently
fetch its own route configuration via the API.

* :ref:`v2 API reference <v2_grpc_streaming_endpoints>`

Statistics
----------

RDS has a statistics tree rooted at *http.<stat_prefix>.rds.<route_config_name>.*.
Any ``:`` character in the ``route_config_name`` name gets replaced with ``_`` in the
stats tree. The stats tree contains the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed because of network errors
  update_rejected, Counter, Total API fetches that failed because of schema/validation errors
  version, Gauge, Hash of the contents from the last successful API fetch
