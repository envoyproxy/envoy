.. _config_http_filters_cors:

CORS
====

This is a filter which handles Cross-Origin Resource Sharing requests based on route or virtual host settings.
For the meaning of the headers please refer to the pages below.

* https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
* https://www.w3.org/TR/cors/
* :ref:`v2 API reference <envoy_api_msg_route.CorsPolicy>`
* This filter should be configured with the name *envoy.cors*.

.. _cors-runtime:

Runtime
-------

The CORS filter supports the following runtime settings:

cors.{runtime_key}.filter_enabled
  The % of requests for which the filter is enabled. Default is 0.
  If present, this will override the :ref: `enabled <envoy_api_msg_route.CorsPolicy>`
  field of the configuration.

cors.{runtime_key}.shadow_enabled
  The % of requests for which the filter is enabled in shadow only mode. Default is 0.
  If present, this will evaluate a requests *Origin* to determine if it's valid
  but will not enforce any policies.

  To determine if shadow mode is enabled you can check the runtime
  values via the admin panel at ``/runtime``.

.. note::

  If both ``filter_enabled`` and ``shadow_enabled`` are on ``filter_enabled``
  will take precedence and policies will be enforced.

.. _cors-statistics:

Statistics
----------

The CORS filter outputs statistics in the <stat_prefix>.cors.* namespace.

.. note::
  Requests that do not have an Origin header will be omitted from statistics.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  origin_valid, Counter, Number of requests that have a valid Origin header.
  origin_invalid, Counter, Number of requests that have an invalid Origin header.
