.. _config_network_filters_rate_limit:

Rate limit
==========

* Global rate limiting :ref:`architecture overview <arch_overview_global_rate_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.ratelimit.v3.RateLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.ratelimit.v3.RateLimit>`

.. note::
  Local rate limiting is also supported via the :ref:`local rate limit filter
  <config_network_filters_local_rate_limit>`.

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
  of :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.network.ratelimit.v3.RateLimit.failure_mode_deny>` set to false."

Runtime
-------

The network rate limit filter supports the following runtime settings:

ratelimit.tcp_filter_enabled
  % of connections that will call the rate limit service. Defaults to 100.

ratelimit.tcp_filter_enforcing
  % of connections that will call the rate limit service and enforce the decision. Defaults to 100.
  This can be used to test what would happen before fully enforcing the outcome.

Dynamic Metadata
----------------
.. _config_network_filters_ratelimit_dynamic_metadata:

The ratelimit filter emits dynamic metadata as an opaque ``google.protobuf.Struct``
*only* when the gRPC ratelimit service returns a :ref:`CheckResponse
<envoy_v3_api_msg_service.ratelimit.v3.RateLimitResponse>` with a filled :ref:`dynamic_metadata
<envoy_v3_api_field_service.ratelimit.v3.RateLimitResponse.dynamic_metadata>` field.

Substitution Formatting
-----------------------

.. _config_network_filters_ratelimit_substitution_formatter:

The network rate limit filter also supports substitution formatting based on stream info populated at request time for its descriptors.
The value field for :ref:`rate_limit_descriptor <envoy_v3_api_field_extensions.filters.network.ratelimit.v3.RateLimit.descriptors>`
accepts runtime substitution.
The format for the substitution formatting can be found in the :ref:`access logging <config_access_log>` documentation

Example usage:

.. code-block:: yaml

  name: envoy.filters.network.ratelimit
  domain: foo
  descriptors:
  - entries:
   - key: remote_address
     value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
   - key: foo
     value: bar
  stat_prefix: name
