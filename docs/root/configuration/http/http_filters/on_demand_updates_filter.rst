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

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
* :ref:`v3 API reference for per route/virtual host config <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.PerRouteConfig>`
* The filter should be placed before *envoy.filters.http.router* filter in the HttpConnectionManager's filter chain.
