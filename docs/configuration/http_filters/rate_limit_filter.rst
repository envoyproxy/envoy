.. _config_http_filters_rate_limit:

Rate limit
==========

Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`.

The HTTP rate limit filter will call the rate limit service when the request's route has one or
more :ref:`rate limit configurations<config_http_conn_man_route_table_route_rate_limits>` that match the
filter stage setting. More than one configuration can apply to a request. Each configuration
results in a descriptor being sent to the rate limit service.

If the rate limit service is called, and the response for any of the descriptors is over limit, a
429 response is returned.

.. code-block:: json

  {
    "type": "decoder",
    "name": "rate_limit",
    "config": {
      "domain": "...",
      "stage": "..."
    }
  }

domain
  *(required, string)* The rate limit domain to use when calling the rate limit service.

stage
  *(optional, integer)* If set, rate limit configurations will only be applied with the same stage
  number. If not set, all rate limit configurations will be applied.

Statistics
----------

The buffer filter outputs statistics in the *cluster.<route target cluster>.ratelimit.* namespace.
429 responses are emitted to the normal cluster :ref:`dynamic HTTP statistics
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total under limit responses from the rate limit service
  error, Counter, Total errors contacting the rate limit service
  over_limit, Counter, total over limit responses from the rate limit service

Runtime
-------

The HTTP rate limit filter supports the following runtime settings:

ratelimit.http_filter_enabled
  % of requests that will call the rate limit service. Defaults to 100.

ratelimit.http_filter_enforcing
  % of requests that will call the rate limit service and enforce the decision. Defaults to 100.
  This can be used to test what would happen before fully enforcing the outcome.

ratelimit.<route_key>.http_filter_enabled
  % of requests that will call the rate limit service for a given *route_key* specified in the
  :ref:`rate limit configuration <config_http_conn_man_route_table_rate_limit_config>`. Defaults to 100.
