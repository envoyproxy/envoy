.. _config_http_filters_body_size_limit:

Body Size Limit
===============

The body size limit filter is used to reject requests whose body size exceeds a configured limit
with an error code of 413.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.body_size_limit.v3.BodySizeLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.body_size_limit.v3.BodySizeLimit>`

Route-Based Filter Control
--------------------------

This filter supports the standard route-based filter chain mechanisms:

* To disable this filter on a specific route or virtual host, use
  :ref:`FilterConfig <envoy_v3_api_msg_config.route.v3.FilterConfig>` with ``disabled: true``
  in ``typed_per_filter_config``.
* To override the filter configuration per-route, use the
  :ref:`filter_chain <config_http_filters_filter_chain>` filter to apply a different
  ``BodySizeLimit`` configuration on specific routes.

See :ref:`route based filter chain <arch_overview_http_filters_route_based_filter_chain>` for
more details.

Statistics
----------

The body size limit filter outputs statistics in the ``http.<stat_prefix>.body_size_limit.``
namespace, where the :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
is the owning HTTP connection manager's.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_content_length_too_large, Counter, Total requests rejected because their ``Content-Length`` exceeded the limit
  rq_body_too_large, Counter, Total requests rejected once the body bytes received exceeded the limit

Dynamic Metadata
----------------

.. _config_http_filters_body_size_limit_dynamic_metadata:

When a request is rejected because its body size exceeds the configured limit, the filter sets
dynamic metadata before sending the ``413`` local reply.

The metadata namespace is the name of the filter as configured in the filter chain (the ``name``
field of the HTTP filter). The namespace contains the following field:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  details, string, "Human-readable details of the form ``length=<bytes>, limit=<bytes>``, describing the observed request length and the configured limit"

For example, with the filter configured as ``body_size_limit``, the value can be
logged using the ``%DYNAMIC_METADATA(body_size_limit:details)%`` access log
format specifier.
