.. _config_thrift_filters_router:

Router
======

The router filter implements Thrift forwarding. It will be used in almost all Thrift proxying
scenarios. The filter's main job is to follow the instructions specified in the configured
:ref:`route table <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.RouteConfiguration>`.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>`

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
  shadow_request_submit_failure, Counter, Total shadow requests that failed to be submitted.


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
  thrift.upstream_resp_exception_local, Counter, Total responses with the "Exception" message type generated locally.
  thrift.upstream_resp_exception_remote, Counter, Total responses with the "Exception" message type received from remote.
  thrift.upstream_resp_invalid_type, Counter, Total responses with an unsupported message type.
  thrift.upstream_resp_decoding_error, Counter, Total responses with an error during decoding.
  thrift.upstream_rq_time, Histogram, total rq time from rq complete to resp complete; includes oneway messages.
  thrift.upstream_rq_size, Histogram, Request message size in bytes per upstream
  thrift.upstream_resp_size, Histogram, Response message size in bytes per upstream
  thrift.upstream_cx_drain_close, Counter, Total upstream connections that were closed due to draining.
  thrift.downstream_cx_partial_response_close, Counter, Total downstream connections that were closed due to the partial response.
  thrift.downstream_cx_underflow_response_close, Counter, Total downstream connections that were closed due to the underflow response.
  thrift.upstream_resp_exception_local.overflow, Counter, Total responses with the "Exception" message type generated locally by connection pool overflow.
  thrift.upstream_resp_exception_local.local_connection_failure, Counter, Total responses with the "Exception" message type generated locally by local connection failure.
  thrift.upstream_resp_exception_local.remote_connection_failure", Counter, Total responses with the "Exception" message type generated locally by remote connection failure.
  thrift.upstream_resp_exception_local.timeout, Counter, Total responses with the "Exception" message type generated locally by timeout while creating a new connection.

If the service zone is available for both the local service (via :option:`--service-zone`)
and the :ref:`upstream cluster <arch_overview_service_discovery_types_eds>`,
Envoy will track the following statistics in *cluster.<name>.zone.<from_zone>.<to_zone>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  thrift.upstream_resp_<\*>, Counter, "Total responses of each type (e.g., reply, success, etc.)"
  thrift.upstream_rq_time, Histogram, Request time milliseconds

.. note::

  The request and response size histograms include what's sent and received during protocol upgrade.
  However, invalid responses are not included in the response size histogram.
