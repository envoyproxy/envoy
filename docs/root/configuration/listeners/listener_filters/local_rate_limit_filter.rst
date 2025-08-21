.. _config_listener_filters_local_rate_limit:

Local rate limit
================

* Local rate limiting :ref:`architecture overview <arch_overview_local_rate_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.local_ratelimit.v3.LocalRateLimit``.
* :ref:`v3 API reference
  <envoy_v3_api_msg_extensions.filters.listener.local_ratelimit.v3.LocalRateLimit>`

.. note::
  The token bucket is shared across all workers, thus the rate limits are applied per Envoy process.

.. note::
  Global rate limiting on network layer is also supported via the :ref:`global rate limit network filter
  <config_network_filters_rate_limit>`.

Overview
--------

The local rate limit filter applies a :ref:`token bucket
<envoy_v3_api_field_extensions.filters.listener.local_ratelimit.v3.LocalRateLimit.token_bucket>` rate
limit to incoming sockets that are processed by the filter's filter chain. Each socket
processed by the filter utilizes a single token, and if no tokens are available, the socket will
be immediately closed without further filter iteration.

.. note::
  In the current implementation each filter and filter chain has an independent rate limit.

.. _config_listener_filters_local_rate_limit_stats:

Statistics
----------

Every configured local rate limit filter has statistics rooted at *listener_local_ratelimit.<stat_prefix>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rate_limited, Counter, Total sockets that have been closed due to rate limit exceeded

Runtime
-------

The local rate limit filter can be runtime feature flagged via the :ref:`enabled
<envoy_v3_api_field_extensions.filters.listener.local_ratelimit.v3.LocalRateLimit.runtime_enabled>`
configuration field.
