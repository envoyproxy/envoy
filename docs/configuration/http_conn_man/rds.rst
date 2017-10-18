.. _config_http_conn_man_rds:

Route discovery service
=======================

The route discovery service (RDS) API is an optional API that Envoy will call to dynamically fetch
:ref:`route configurations <config_http_conn_man_route_table>`. A route configuration includes both
HTTP header modifications, virtual hosts, and the individual route entries contained within each
virtual host. Each :ref:`HTTP connection manager filter <config_http_conn_man>` can independently
fetch its own route configuration via the API.

.. code-block:: json

  {
    "cluster": "...",
    "route_config_name": "...",
    "refresh_delay_ms": "..."
  }

cluster
  *(required, string)* The name of an upstream :ref:`cluster <config_cluster_manager_cluster>` that
  hosts the route discovery service. The cluster must run a REST service that implements the
  :ref:`RDS HTTP API <config_http_conn_man_rds_api>`. NOTE: This is the *name* of a cluster defined
  in the :ref:`cluster manager <config_cluster_manager>` configuration, not the full definition of
  a cluster as in the case of SDS and CDS.

route_config_name
  *(required, string)* The name of the route configuration. This name will be passed to the
  :ref:`RDS HTTP API <config_http_conn_man_rds_api>`. This allows an Envoy configuration with
  multiple HTTP listeners (and associated HTTP connection manager filters) to use different route
  configurations. By default, the maximum length of the name is limited to 60 characters. This
  limit can be increased by setting the :option:`--max-obj-name-len` command line argument to the
  desired value.

refresh_delay_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the RDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_delay_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_delay_ms*. Default
  value is 30000ms (30 seconds).

.. _config_http_conn_man_rds_api:

REST API
--------

.. http:get:: /v1/routes/(string: route_config_name)/(string: service_cluster)/(string: service_node)

Asks the route discovery service to return the route configuration for a particular
`route_config_name`, `service_cluster`, and `service_node`. `route_config_name` corresponds to the
RDS configuration parameter above. `service_cluster` corresponds to the :option:`--service-cluster`
CLI option. `service_node` corresponds to the :option:`--service-node` CLI option. Responses are a
single JSON object that contains a route configuration as defined in the :ref:`route configuration
documentation <config_http_conn_man_route_table>`.

A new route configuration will be gracefully swapped in such that existing requests are not
affected. This means that when a request starts, it sees a consistent snapshot of the route
configuration that does not change for the duration of the request. Thus, if an update changes a
timeout for example, only new requests will use the updated timeout value.

As a performance optimization, Envoy hashes the route configuration it receives from the RDS API and
will only perform a full reload if the hash value changes.

.. attention::

  Route configurations that are loaded via RDS are *not* checked to see if referenced clusters are
  known to the :ref:`cluster manager <config_cluster_manager>`. The RDS API has been designed to
  work alongside the :ref:`CDS API <config_cluster_manager_cds>` such that Envoy assumes eventually
  consistent updates. If a route references an unknown cluster a 404 response will be returned by
  the router filter.

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
  update_failure, Counter, Total API fetches that failed (either network or schema errors)
  version, Gauge, Hash of the contents from the last successful API fetch
