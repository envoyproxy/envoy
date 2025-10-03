.. _config_http_filters_on_demand:

On-demand VHDS, S/RDS and CDS Updates
=====================================

The on demand filter can be used to support either on demand VHDS or S/RDS update together with on demand CDS update
if configured in the filter chain.

The on-demand update filter can be used to request a :ref:`virtual host <envoy_v3_api_msg_config.route.v3.VirtualHost>`
data if it's not already present in the :ref:`Route Configuration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`. The
contents of the *Host* or *:authority* header is used to create the on-demand request. For an on-demand
request to be created, :ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` must be enabled and either *Host*
or *:authority* header be present.

The on-demand update filter can also be used to request a *Route Configuration* data if RouteConfiguration is specified to be
loaded on demand in the :ref:`Scoped RouteConfiguration <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>`.
The contents of the HTTP header is used to find the scope and create the on-demand request.

The on-demand update filter can also be used to request a cluster when the target cluster name is only known after receiving
a request and the cluster is missing - this is a scenario that could happen with
the :ref:`cluster_header <envoy_v3_api_field_config.route.v3.RouteAction.cluster_header>` route action. For an on demand
request to be created, :ref:`odcds <envoy_v3_api_field_extensions.filters.http.on_demand.v3.OnDemand.odcds>`
(or its :ref:`per route counterpart <envoy_v3_api_field_extensions.filters.http.on_demand.v3.PerRouteConfig.odcds>`)
must be specified and a header specified in :ref:`cluster_header <envoy_v3_api_field_config.route.v3.RouteAction.cluster_header>`
action must be present in the HTTP request.

On-demand VHDS and on-demand S/RDS can not be used at the same time at this point.

On-demand CDS can also be enabled or disabled per virtual host or route. Specifying an extension config
in :ref:`virtual host's typed_per_filter_config <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>` or
the :ref:`route's typed_per_filter_config <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>` without
the :ref:`odcds <envoy_v3_api_field_extensions.filters.http.on_demand.v3.OnDemand.odcds>` field disables
the on demand CDS for requests using this virtual host or route. Conversely,
if :ref:`odcds <envoy_v3_api_field_extensions.filters.http.on_demand.v3.OnDemand.odcds>` is specified,
on demand CDS is enabled for requests using this virtual host or route.

Per-route configuration limitations
------------------------------------

.. warning::

   **IMPORTANT LIMITATION**: When VH discovery brings in per-route filter configuration overrides,
   requests with body data cannot use stream recreation because it would lose the buffered request body.
   This creates inconsistent behavior where:

   - Bodyless requests (``GET``, ``HEAD``, etc.) receive per-route config overrides ✓
   - Requests with body (``POST``, ``PUT``, etc.) do NOT receive per-route config overrides ✗

The filter provides a configuration option ``allow_body_data_loss_for_per_route_config`` to control this behavior:

- If ``false`` (default): Requests with body continue with original filter chain configuration to preserve body data. Per-route overrides are NOT applied. This is the safest option but creates inconsistent behavior.

- If ``true``: Requests with body will attempt stream recreation to apply per-route overrides, but this will LOSE the buffered request body data. Only enable this if you understand the data loss implications.

**Recommendation**: Keep this ``false`` unless you have a specific need for consistent per-route configuration behavior and can tolerate request body loss.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
* :ref:`v3 API reference for per route/virtual host config <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.PerRouteConfig>`
* The filter should be placed before *envoy.filters.http.router* filter in the HttpConnectionManager's filter chain.

Example Configuration
~~~~~~~~~~~~~~~~~~~~~

Basic configuration with default behavior (preserves request body, may not apply per-route overrides):

.. literalinclude:: /_configs/on_demand/basic-config.yaml
    :language: yaml
    :linenos:
    :caption: Basic on-demand filter configuration

Configuration allowing body data loss for consistent per-route behavior:

.. literalinclude:: /_configs/on_demand/allow-body-loss-config.yaml
    :language: yaml
    :linenos:
    :caption: Configuration with body data loss allowed

Complete example showing the filter in an HTTP connection manager context:

.. literalinclude:: /_configs/on_demand/complete-config.yaml
    :language: yaml
    :linenos:
    :caption: Complete Envoy configuration with on-demand filter
