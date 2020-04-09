.. _config_thrift_filters_router:

Router
======

The router filter implements Thrift forwarding. It will be used in almost all Thrift proxying
scenarios. The filter's main job is to follow the instructions specified in the configured
:ref:`route table <envoy_api_msg_config.filter.network.thrift_proxy.v2alpha1.RouteConfiguration>`.

* :ref:`v2 API reference <envoy_api_msg_config.filter.thrift.router.v2alpha1.Router>`
* This filter should be configured with the name *envoy.filters.thrift.router*.

Statistics
----------

The filter outputs statistics in the *thrift.<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  route_missing, Counter, Total requests with no route found.
  unknown_cluster, Counter, Total requests with a route that has an unknown cluster.
  upstream_rq_maintenance_mode, Counter, Total requests with a destination cluster in maintenance mode.
  no_healthy_upstream, Counter, Total requests with no healthy upstream endpoints available.
