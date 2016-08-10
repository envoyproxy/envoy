.. _config_network_filters_rate_limit:

Rate limit
==========

Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`.

.. code-block:: json

  {
    "type": "read",
    "name": "ratelimit",
    "config": {
      "stat_prefix": "...",
      "domain": "...",
      "descriptors": []
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

.. _config_network_filters_rate_limit_stats:

Statistics
----------

Every configured rate limit filter has statistics rooted at *ratelimit.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Description
  error, Counter, Description
  over_limit, Counter, Description
  ok, Counter, Description
  cx_closed, Counter, Description
  active, Gauge, Description

Runtime
-------

The network rate limit filter supports the following runtime settings:

ratelimit.tcp_filter_enabled
  FIXFIX

ratelimit.tcp_filter_enforcing
  FIXFIX
