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
The fraction of requests for which the filter is enabled can be configured via the :ref:`runtime_key
<envoy_api_field_core.runtimefractionalpercent.runtime_key>` value of the :ref:`filter_enabled
<envoy_api_field_route.CorsPolicy.filter_enabled>` field.

The fraction of requests for which the filter is enabled in shadow-only mode can be configured via
the :ref:`runtime_key <envoy_api_field_core.runtimefractionalpercent.runtime_key>` value of the
:ref:`shadow_enabled <envoy_api_field_route.CorsPolicy.shadow_enabled>` field. When enabled in
shadow-only node, the filter will evaluate the request's *Origin* to determine if it's valid but
will not enforce any policies.

.. note::

  If both ``filter_enabled`` and ``shadow_enabled`` are on, the ``filter_enabled``
  flag will take precedence.

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
