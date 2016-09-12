.. _config_http_filters_health_check:

Health check
============

Health check filter :ref:`architecture overview <arch_overview_health_checking_filter>`.

.. code-block:: json

  {
    "type": "both",
    "name": "health_check",
    "config": {
      "pass_through_mode": "...",
      "endpoint": "...",
      "cache_time_ms": "...",
     }
  }

pass_through_mode
  *(required, boolean)* Specifies whether the filter operates in pass through mode or not.

endpoint
  *(required, string)* Specifies the incoming HTTP endpoint that should be considered the
  health check endpoint. For example */healthcheck*.

cache_time_ms
  *(optional, integer)* If operating in pass through mode, the amount of time in milliseconds that
  the filter should cache the upstream response.
