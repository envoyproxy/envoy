.. _config_http_filters_rate_limit:

Rate limit
==========

Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`.

The HTTP rate limit filter will call the rate limit service when the request's route has the
*global* property set in the :ref:`rate limit configuration
<config_http_conn_man_route_table_route_rate_limit>`. If the rate limit service is called, the
following descriptors are sent:

  * ("to_cluster", "<:ref:`route target cluster <config_http_conn_man_route_table_route_cluster>`>")
  * ("to_cluster", "<:ref:`route target cluster <config_http_conn_man_route_table_route_cluster>`>"),
    ("from_cluster", "<local service cluster>")

<local service cluster> is derived from the :option:`--service-cluster` option.

If the rate limit service is called, and the response for either of the above descriptors is over
limit, a 429 response is returned.

.. code-block:: json

  {
    "type": "decoder",
    "name": "rate_limit",
    "config": {
      "domain": "..."
    }
  }

domain
  *(required, string)* The rate limit domain to use when calling the rate limit service.

Statistics
----------

The buffer filter outputs statistics in the *cluster.<route target cluster>.ratelimit.* namespace.
429 responses are emitted to the normal cluster :ref:`dynamic HTTP statistics
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Description
  error, Counter, Description
  over_limit, Counter, Description

Runtime
-------

The HTTP rate limit filter supports the following runtime settings:

ratelimit.http_filter_enabled
  FIXFIX

ratelimit.http_filter_enforcing
  FIXFIX
