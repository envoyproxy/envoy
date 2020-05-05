.. _config_http_conn_man_rds:

Route discovery service (RDS)
=============================

The route discovery service (RDS) API is an optional API that Envoy will call to dynamically fetch
:ref:`route configurations <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`. A route configuration includes both
HTTP header modifications, virtual hosts, and the individual route entries contained within each
virtual host. Each :ref:`HTTP connection manager filter <config_http_conn_man>` can independently
fetch its own route configuration via the API. Optionally, the 
:ref:`virtual host discovery service <config_http_conn_man_vhds>`
can be used to fetch virtual hosts separately from the route configuration.

* :ref:`v2 API reference <v2_grpc_streaming_endpoints>`

Statistics
----------

RDS has a :ref:`statistics <subscription_statistics>` tree rooted at *http.<stat_prefix>.rds.<route_config_name>.*.
Any ``:`` character in the ``route_config_name`` name gets replaced with ``_`` in the
stats tree.
