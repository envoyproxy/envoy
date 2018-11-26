.. _config_network_filters_rate_limit:

Rate limit
==========

* Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.network.rate_limit.v2.RateLimit>`

.. _config_network_filters_rate_limit_stats:

Statistics
----------

Every configured rate limit filter has statistics rooted at *ratelimit.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Total requests to the rate limit service
  error, Counter, Total errors contacting the rate limit service
  over_limit, Counter, Total over limit responses from the rate limit service
  ok, Counter, Total under limit responses from the rate limit service
  cx_closed, Counter, Total connections closed due to an over limit response from the rate limit service
  active, Gauge, Total active requests to the rate limit service
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of :ref:`failure_mode_deny <envoy_api_msg_config.filter.http.rate_limit.v2.RateLimit>` set to false."

Runtime
-------

The network rate limit filter supports the following runtime settings:

ratelimit.tcp_filter_enabled
  % of connections that will call the rate limit service. Defaults to 100.

ratelimit.tcp_filter_enforcing
  % of connections that will call the rate limit service and enforce the decision. Defaults to 100.
  This can be used to test what would happen before fully enforcing the outcome.
