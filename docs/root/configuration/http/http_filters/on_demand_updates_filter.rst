.. _config_http_filters_on_demand:

On-demand VHDS Updates
======================

The on-demand VHDS filter is used to request a :ref:`virtual host <envoy_v3_api_msg_config.route.v3.VirtualHost>`
data if it's not already present in the :ref:`Route Configuration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`. The
contents of the *Host* or *:authority* header is used to create the on-demand request. For an on-demand
request to be created, :ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` must be enabled and either *Host*
or *:authority* header be present.

On-demand VHDS cannot be used with SRDS at this point.

Configuration
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
* This filter should be configured with the name *envoy.filters.http.on_demand*.
* The filter should be placed before *envoy.filters.http.router* filter in the HttpConnectionManager's filter chain.
