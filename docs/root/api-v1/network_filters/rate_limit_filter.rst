.. _config_network_filters_rate_limit_v1:

Rate limit
==========

Rate limit :ref:`configuration overview <config_network_filters_rate_limit>`.

.. code-block:: json

  {
    "name": "ratelimit",
    "config": {
      "stat_prefix": "...",
      "domain": "...",
      "descriptors": [],
      "timeout_ms": "..."
    }
  }

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_rate_limit_stats>`.

domain
  *(required, string)* The rate limit domain to use in the rate limit service request.

descriptors
  *(required, array)* The rate limit descriptor list to use in the rate limit service request. The
  descriptors are specified as in the following example:

  .. code-block:: json

    [
      [{"key": "hello", "value": "world"}, {"key": "foo", "value": "bar"}],
      [{"key": "foo2", "value": "bar2"}]
    ]

timeout_ms
  *(optional, integer)* The timeout in milliseconds for the rate limit service RPC. If not set,
  this defaults to 20ms.
