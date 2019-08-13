.. _config_thrift_filters_rate_limit:

Rate limit
==========

* Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.thrift.rate_limit.v2alpha1.RateLimit>`
* This filter should be configured with the name *envoy.ratelimit*.

The Thrift rate limit filter will call the rate limit service when the request's route has one or
more :ref:`rate limit configurations
<envoy_api_field_config.filter.network.thrift_proxy.v2alpha1.RouteAction.rate_limits>` that
match the filter's stage setting. More than one configuration can apply to a request. Each
configuration results in a descriptor being sent to the rate limit service.

If the rate limit service is called, and the response for any of the descriptors is over limit, an
application exception indicating an internal error is returned.

If there is an error in calling the rate limit service or it returns an error and
:ref:`failure_mode_deny
<envoy_api_field_config.filter.thrift.rate_limit.v2alpha1.RateLimit.failure_mode_deny>` is set to
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
  <envoy_api_field_config.filter.thrift.rate_limit.v2alpha1.RateLimit.failure_mode_deny>` set to
  false."
