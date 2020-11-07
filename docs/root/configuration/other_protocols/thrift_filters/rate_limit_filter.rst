.. _config_thrift_filters_rate_limit:

Rate limit
==========

* Global rate limiting :ref:`architecture overview <arch_overview_global_rate_limit>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit>`
* This filter should be configured with the name *envoy.filters.thrift.rate_limit*.

The Thrift rate limit filter will call the rate limit service when the request's route has one or
more :ref:`rate limit configurations
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteAction.rate_limits>` that
match the filter's stage setting. More than one configuration can apply to a request. Each
configuration results in a descriptor being sent to the rate limit service.

If the rate limit service is called, and the response for any of the descriptors is over limit, an
application exception indicating an internal error is returned.

If there is an error in calling the rate limit service or it returns an error and
:ref:`failure_mode_deny
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit.failure_mode_deny>` is set to
true, an application exception indicating an internal error is returned.

.. _config_thrift_filters_rate_limit_stats:

Statistics
----------

The filter outputs statistics in the *cluster.<route target cluster>.ratelimit.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total under limit responses from the rate limit service.
  error, Counter, Total errors contacting the rate limit service.
  over_limit, Counter, Total over limit responses from the rate limit service.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of :ref:`failure_mode_deny
  <envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit.failure_mode_deny>` set to
  false."
