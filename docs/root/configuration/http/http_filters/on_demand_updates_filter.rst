.. _config_http_filters_on_demand:

On Demand VHDS Updates
======================

The On Demand VHDS Updates filter is used to request a :ref:`virtual host <envoy_api_msg_route.VirtualHost>`
data if it's not already present in the :ref:`Route Configuration <envoy_api_msg_RouteConfiguration>`. The
contents of the "Host" or ":authority" header is used to create the on-demand request. For an on-demand
request to be created, :ref:`VHDS <envoy_api_field_RouteConfiguration.vhds>` must be enabled and either "Host"
or ":authority" header be present.

Configuration
-------------
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.on_demand.v2.OnDemand>`
* The filter name is *envoy.on_demand*. The On Demand VHDS Updates filter is automatically configured at the
  head of an http filter chain.
