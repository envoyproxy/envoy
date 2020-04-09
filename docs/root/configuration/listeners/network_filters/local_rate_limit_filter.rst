.. _config_network_filters_local_rate_limit:

Local rate limit
================

* Local rate limiting :ref:`architecture overview <arch_overview_local_rate_limit>`
* :ref:`v2 API reference
  <envoy_api_msg_config.filter.network.local_rate_limit.v2alpha.LocalRateLimit>`
* This filter should be configured with the name *envoy.filters.network.local_ratelimit*.

.. note::
  Global rate limiting is also supported via the :ref:`global rate limit filter
  <config_network_filters_rate_limit>`.

Overview
--------

The local rate limit filter applies a :ref:`token bucket
<envoy_api_field_config.filter.network.local_rate_limit.v2alpha.LocalRateLimit.token_bucket>` rate
limit to incoming connections that are processed by the filter's filter chain. Each connection
processed by the filter utilizes a single token, and if no tokens are available, the connection will
be immediately closed without further filter iteration.

.. note::
  In the current implementation each filter and filter chain has an independent rate limit.

.. _config_network_filters_local_rate_limit_stats:

Statistics
----------

Every configured local rate limit filter has statistics rooted at *local_ratelimit.<stat_prefix>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rate_limited, Counter, Total connections that have been closed due to rate limit exceeded

Runtime
-------

The local rate limit filter can be runtime feature flagged via the :ref:`enabled
<envoy_api_field_config.filter.network.local_rate_limit.v2alpha.LocalRateLimit.runtime_enabled>`
configuration field.
