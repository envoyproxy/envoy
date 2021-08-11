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


The filter is also responsible for cluster-level statistics derived from routed upstream clusters.
Since these stats utilize the underlying cluster scope, we prefix with the ``thrift`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  thrift.upstream_rq_call, Counter, Total requests with the "Call" message type.
  thrift.upstream_rq_oneway, Counter, Total requests with the "Oneway" message type.
  thrift.upstream_rq_invalid_type, Counter, Total requests with an unsupported message type.
  thrift.upstream_resp_reply, Counter, Total responses with the "Reply" message type. Sums both Successses and Errors.
  thrift.upstream_resp_success, Counter, Total Replies that are considered "Successes".
  thrift.upstream_resp_error, Counter, Total Replies that are considered "Errors".
  thrift.upstream_resp_exception, Counter, Total responses with the "Exception" message type.
  thrift.upstream_resp_invalid_type, Counter, Total responses with an unsupported message type.
  thrift.upstream_rq_time, Histogram, total rq time from rq complete to resp complete; includes oneway messages.
  thrift.upstream_rq_size, Histogram, Request message size in bytes per upstream
  thrift.upstream_resp_size, Histogram, Response message size in bytes per upstream

.. note::

  The request and response size histograms include what's sent and received during protocol upgrade.
  However, invalid responses are not included in the response size histogram.
