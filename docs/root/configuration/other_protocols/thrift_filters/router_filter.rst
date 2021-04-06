.. _config_thrift_filters_router:

Router
======

The router filter implements Thrift forwarding. It will be used in almost all Thrift proxying
scenarios. The filter's main job is to follow the instructions specified in the configured
:ref:`route table <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.RouteConfiguration>`.

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>`
* This filter should be configured with the name *envoy.filters.thrift.router*.

Statistics
----------

The filter outputs generic routing error statistics in the *thrift.<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  route_missing, Counter, Total requests with no route found.
  unknown_cluster, Counter, Total requests with a route that has an unknown cluster.
  upstream_rq_maintenance_mode, Counter, Total requests with a destination cluster in maintenance mode.
  no_healthy_upstream, Counter, Total requests with no healthy upstream endpoints available.


The filter also outputs MessageType statistics in the upstream cluster's stat scope.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request_call, Counter, Total requests with the "Call" message type.
  request_oneway, Counter, Total requests with the "Oneway" message type.
  request_invalid_type, Counter, Total requests with an unsupported message type.
  response_reply, Counter, Total responses with the "Reply" message type. Includes both successes and errors.
  response_exception, Counter, Total responses with the "Exception" message type.
  response_invalid_type, Counter, Total responses with an unsupported message type.
