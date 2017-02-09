.. _config_http_conn_man_rds:

Route discovery service
=======================

The route discovery service (RDS) API is an optional API that Envoy will call to dynamically fetch
route tables. Each :ref:`HTTP connection manager filter <config_http_conn_man>` can independently
fetch its own route table via the API.

.. code-block:: json

  {
    "cluster": "{...}",
    "route_config_name": "...",
    "refresh_interval_ms": "..."
  }

cluster
  *(required, string)* The name of an upstream :ref:`cluster <config_cluster_manager_cluster>` that
  hosts the route discovery service. The cluster must run a REST service that implements the
  :ref:`RDS HTTP API <config_http_conn_man_rds_api>`.

route_config_name
  *(required, string)* The name of the route table. This name will be passed to the
  :ref:`RDS HTTP API <config_http_conn_man_rds_api>` so that different route tables can be used by
  independent filters.

refresh_interval_ms
  *(optional, integer)* The delay, in milliseconds, between fetches to the RDS API. Envoy will add
  an additional random jitter to the delay that is between zero and *refresh_interval_ms*
  milliseconds. Thus the longest possible refresh delay is 2 \* *refresh_interval_ms*. Default
  value is 30000ms (30 seconds).

.. _config_http_conn_man_rds_api:

REST API
--------

.. http:get:: /v1/routes/(string: route_config_name)/(string: service_cluster)/(string: service_node)

Asks the discovery service to return the route table for a particular `route_config_name`,
`service_cluster`, and `service_node`. `route_config_name` corresponds to the RDS configuration
parameter above. `service_cluster` corresponds to the :option:`--service-cluster` CLI option.
`service_node` corresponds to the :option:`--service-node` CLI option. Responses are a single JSON
object that contains a route table as defined in the :ref:`route table documentation
<config_http_conn_man_route_table>`.

A new route table will be gracefully swapped in such that existing requests are not effected.

.. attention::

  Route tables that are loaded via RDS are *not* checked to see if referenced clusters are known
  to the :ref:`cluster manager <config_cluster_manager>`. The RDS API has been designed to work
  alongside the :ref:`CDS API <config_cluster_manager_cds>` such that Envoy assumes eventually
  consistent updates. If a route references an unknown cluster a 404 response will be returned by
  the router filter.

Statistics
----------

RDS has a statistics tree rooted at *http.<stat_prefix>.rds.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total API fetches attempted
  update_success, Counter, Total API fetches completed successfully
  update_failure, Counter, Total API fetches that failed (either network or schema errors)
