.. _config_http_conn_man_rds_v1:

Route discovery service (RDS)
=============================

.. code-block:: json

  {
    "cluster": "...",
    "route_config_name": "...",
    "refresh_delay_ms": "..."
  }

cluster
  *(required, string)* The name of an upstream :ref:`cluster <config_cluster_manager_cluster>` that
  hosts the route discovery service. The cluster must run a REST service that implements the
  :ref:`RDS HTTP API <config_http_conn_man_rds_v1_api>`. NOTE: This is the *name* of a statically defined
  cluster in the :ref:`cluster manager <config_cluster_manager>` configuration, not the full definition of
  a cluster as in the case of SDS and CDS.

route_config_name
  *(required, string)* The name of the route configuration. This name will be passed to the
  :ref:`RDS HTTP API <config_http_conn_man_rds_v1_api>`. This allows an Envoy configuration with
  multiple HTTP listeners (and associated HTTP connection manager filters) to use different route
  configurations. By default, the maximum length of the name is limited to 60 characters. This
  limit can be increased by setting the :option:`--max-obj-name-len` command line argument to the
  desired value.

refresh_delay_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the RDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_delay_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_delay_ms*. Default
  value is 30000ms (30 seconds).

.. _config_http_conn_man_rds_v1_api:

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
