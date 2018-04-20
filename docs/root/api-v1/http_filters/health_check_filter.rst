.. _config_http_filters_health_check_v1:

Health check
============

Health check :ref:`configuration overview <config_http_filters_health_check>`.

.. code-block:: json

  {
    "name": "health_check",
    "config": {
      "pass_through_mode": "...",
      "endpoint": "...",
      "cache_time_ms": "..."
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
