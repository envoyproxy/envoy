.. _config_http_filters_on_demand:

On-demand VHDS Updates
======================

The on-demand VHDS filter is used to request a :ref:`virtual host <envoy_api_msg_route.VirtualHost>`
data if it's not already present in the :ref:`Route Configuration <envoy_api_msg_RouteConfiguration>`. The
contents of the *Host* or *:authority* header is used to create the on-demand request. For an on-demand
request to be created, :ref:`VHDS <envoy_api_field_RouteConfiguration.vhds>` must be enabled and either *Host*
or *:authority* header be present.

On-demand VHDS cannot be used with SRDS at this point.

Configuration
-------------
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.on_demand.v2.OnDemand>`
* This filter should be configured with the name *envoy.filters.http.on_demand*.
* The filter should be placed before *envoy.filters.http.router* filter in the HttpConnectionManager's filter chain.
