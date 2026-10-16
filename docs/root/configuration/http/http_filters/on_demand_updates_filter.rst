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

Statistics
----------

The on-demand CDS config sources emit statistics rooted at *cluster_manager.odcds.*, documented in
the :ref:`cluster manager statistics <config_cluster_manager_odcds_stats>`.

The filter also emits statistics under the HTTP connection manager's stat prefix, as
``http.<stat_prefix>.on_demand.``. They only account for requests that actually had to wait for a
discovery; a request whose route and cluster are already known does not touch them. Route discovery
covers on-demand VHDS and on-demand S/RDS, which the filter cannot tell apart. A route discovery
counts as found when the virtual host (or scoped route configuration) was delivered.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  route_discovery_requested, Counter, Total on-demand route discoveries started.
  route_discovery_found, Counter, Route discoveries that completed with the route configuration available.
  route_discovery_not_found, Counter, Route discoveries that completed without the route configuration becoming available.
  route_discovery_cancelled, Counter, Route discoveries abandoned because the stream was destroyed before they completed.
  route_discovery_pending, Gauge, Route discoveries currently in flight.
  route_discovery_duration, Histogram, Time from request to completion of route discoveries in milliseconds.
  cluster_discovery_requested, Counter, Total on-demand cluster discoveries started.
  cluster_discovery_available, Counter, Cluster discoveries that completed with the cluster available.
  cluster_discovery_missing, Counter, Cluster discoveries where on-demand CDS reported that the cluster does not exist.
  cluster_discovery_timeout, Counter, Cluster discoveries that timed out before any reply.
  cluster_discovery_cancelled, Counter, Cluster discoveries abandoned because the stream was destroyed before they completed.
  cluster_discovery_pending, Gauge, Cluster discoveries currently in flight.
  cluster_discovery_duration, Histogram, Time from request to completion of cluster discoveries in milliseconds.

Access log timing
-----------------

For every discovery that was actually started, the filter latches monotonic timestamps into the
stream's downstream timing, which can be used with the
:ref:`COMMON_DURATION <config_access_log_format_common_duration>` access log command:

* ``envoy.filters.http.on_demand.route_discovery_start`` and ``envoy.filters.http.on_demand.route_discovery_end``
* ``envoy.filters.http.on_demand.cluster_discovery_start`` and ``envoy.filters.http.on_demand.cluster_discovery_end``

A key is not set if the corresponding discovery did not happen, so the command renders ``-`` for
requests that did not wait. The end key is also set when a discovery fails or times out. For example,
to log the time spent in route discovery in microseconds:

.. code-block:: none

  %COMMON_DURATION(envoy.filters.http.on_demand.route_discovery_start:envoy.filters.http.on_demand.route_discovery_end:us)%

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
* :ref:`v3 API reference for per route/virtual host config <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.PerRouteConfig>`
* The filter should be placed before *envoy.filters.http.router* filter in the HttpConnectionManager's filter chain.
