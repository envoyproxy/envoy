.. _arch_overview_access_logs:

Access logging
==============

The :ref:`HTTP connection manager <arch_overview_http_conn_man>`, the
:ref:`tcp proxy <arch_overview_tcp_proxy>` and the
:ref:`thrift proxy <config_network_filters_thrift_proxy>`
support extensible access logging with the following features:

* Any number of access logs per a connection stream.
* Customizable access log filters that allow different types of requests and responses to be written
  to different access logs.

Downstream connection access logging can be enabled using :ref:`listener access
logs<envoy_v3_api_field_config.listener.v3.Listener.access_log>`. The listener access logs complement
HTTP request access logging and can be enabled separately and independently from
filter access logs.

If access log is enabled, then by default it will be reported to the configured sinks at the end of a UDP
session, TCP connection, or HTTP stream. It is possible to extend this behavior and report access logs
periodically or at the start of a UDP session, TCP connection, or HTTP stream. Reporting access logs right
upstream connection establishment or new incoming HTTP request does not depend on periodic reporting, and
the other way around.

.. _arch_overview_access_log_start:

Start of session access logs
----------------------------

UDP Proxy
*********

For UDP Proxy, when UDP tunneling over HTTP is configured, it is possible to enable an access log record once after a successful upstream tunnel connected by using
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.UdpAccessLogOptions.flush_access_log_on_tunnel_connected>`

TCP Proxy
*********

For TCP Proxy, it is possible to enable an access log record once after a successful upstream connection by using
:ref:`flush access log on connected <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TcpAccessLogOptions.flush_access_log_on_connected>`

HTTP Connection Manager
***********************

For HTTP Connection Manager, it is possible to enable an access log once when a new HTTP request is received, and before iterating the filter chain by using
:ref:`flush access log on new request <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.HcmAccessLogOptions.flush_access_log_on_new_request>`
Note: Some information such as upstream host will not be available yet.

HTTP Router Filter
******************

For Router Filter, is is possible to enable an upstream access log when a new upstream stream is associated with the downstream stream,
and after successfully establishing a connection with the upstream by using
:ref:`flush upstream log on upstream stream <envoy_v3_api_field_extensions.filters.http.router.v3.Router.UpstreamAccessLogOptions.flush_upstream_log_on_upstream_stream>`
Note: In case that the HTTP request involves retries, a start of request upstream access log will be recorded for each retry.

.. _arch_overview_access_log_periodic:

Periodic access logs
--------------------

UDP Proxy
*********

For UDP Proxy, it is possible to enable a prediodic access log by using
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.UdpAccessLogOptions.access_log_flush_interval>`

TCP Proxy
*********

For TCP Proxy, it is possible to enable a prediodic access log by using
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TcpAccessLogOptions.access_log_flush_interval>`
Note: The first access log entry is generated one interval after a new connection is received by the TCP Proxy whether or not an upstream connection has been made.

HTTP Connection Manager
***********************

For HTTP Connection Manager, it is possible to enable a prediodic access log by using
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.HcmAccessLogOptions.access_log_flush_interval>`
Note: The first access log entry is generated one interval after a new HTTP request is received by the HTTP Connection Manager and before iterating
the HTTP filter chain, whether or not an upstream connection has been made.

HTTP Router Filter
******************

For Router Filter, it is possible to enable a prediodic access log by using
:ref:`upstream log flush interval <envoy_v3_api_field_extensions.filters.http.router.v3.Router.UpstreamAccessLogOptions.upstream_log_flush_interval>`
Note: The first access log entry is generated one interval after a new HTTP request is received by the router filter, whether or not an upstream connection has been made.

.. _arch_overview_access_log_filters:

Access log filters
------------------

Envoy supports several built-in
:ref:`access log filters<envoy_v3_api_msg_config.accesslog.v3.AccessLogFilter>` and
:ref:`extension filters<envoy_v3_api_field_config.accesslog.v3.AccessLogFilter.extension_filter>`
that are registered at runtime.

.. _arch_overview_access_logs_sinks:

Access logging sinks
--------------------

Envoy supports pluggable access logging sinks. The currently supported sinks are:

File
****

* Asynchronous IO flushing architecture. Access logging will never block the main network processing
  threads.
* Customizable access log formats using predefined fields as well as arbitrary HTTP request and
  response headers.

gRPC
****

* Envoy can send access log messages to a gRPC access logging service.


Stdout
*********

* Asynchronous IO flushing architecture. Access logging will never block the main network processing
  threads.
* Customizable access log formats using predefined fields as well as arbitrary HTTP request and
  response headers.
* Writes to the standard output of the process. It works in all platforms.


Stderr
********

* Asynchronous IO flushing architecture. Access logging will never block the main network processing
  threads.
* Customizable access log formats using predefined fields as well as arbitrary HTTP request and
  response headers.
* Writes to the standard error of the process. It works in all platforms.

Further reading
---------------

* Access log :ref:`configuration <config_access_log>`.
* File :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.file.v3.FileAccessLog>`.
* gRPC :ref:`Access Log Service (ALS) <envoy_v3_api_msg_extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig>`
  sink.
* OpenTelemetry (gRPC) :ref:`LogsService <envoy_v3_api_msg_extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig>`
* Stdout :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StdoutAccessLog>`
* Stderr :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StderrAccessLog>`
