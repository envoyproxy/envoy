.. _arch_overview_access_logs:

Access logging
==============

The :ref:`HTTP connection manager <arch_overview_http_conn_man>`, the
:ref:`tcp proxy <arch_overview_tcp_proxy>` and the
:ref:`thrift proxy <config_network_filters_thrift_proxy>`
support extensible access logging with the following features:

* Multiple access logs per connection stream.
* Customizable access log filters for routing different requests/responses to separate logs.
* Independent downstream connection logging via listener access logs.

Downstream connection access logging can be enabled using :ref:`listener access
logs<envoy_v3_api_field_config.listener.v3.Listener.access_log>`. The listener access logs complement
HTTP request access logging and can be enabled separately and independently from filter access logs.

By default, if access logging is enabled, logs are sent to the configured sinks at the end of each UDP session,
TCP connection, or HTTP stream. However, it is possible to extend this behavior and report access logs periodically or
at the start of a UDP session, TCP connection, or HTTP stream. Generating access logs at the start of an upstream
connection or request does not depend on periodic logging, and vice versa.

.. _arch_overview_access_log_start:

Start of session access logs
----------------------------

UDP Proxy
*********

For UDP Proxy, when UDP tunneling over HTTP is configured, it is possible to enable an access log record once after a
successful upstream tunnel connection is established by enabling
:ref:`flush access log on tunnel connected <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.UdpAccessLogOptions.flush_access_log_on_tunnel_connected>`.

TCP Proxy
*********

For TCP Proxy, it is possible to enable a one-time access log entry right after a successful upstream connection by enabling
:ref:`flush access log on connected <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TcpAccessLogOptions.flush_access_log_on_connected>`

HTTP Connection Manager
***********************

For HTTP Connection Manager, it is possible to enable a one-time access log entry each time a new HTTP request arrives,
and before the filter chain is processed by enabling
:ref:`flush access log on new request <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.HcmAccessLogOptions.flush_access_log_on_new_request>`

.. note::
   Some information such as upstream host will not be available yet.

HTTP Router Filter
******************

For Router Filter, it is possible to enable one-time upstream access log entry each time a new upstream stream is
associated with a downstream stream, after the connection with the upstream is established, by enabling
:ref:`flush upstream log on upstream stream <envoy_v3_api_field_extensions.filters.http.router.v3.Router.UpstreamAccessLogOptions.flush_upstream_log_on_upstream_stream>`

.. note::
   If the HTTP request involves retries, a start-of-request upstream access log is generated for each retry attempt.

.. _arch_overview_access_log_periodic:

Periodic access logs
--------------------

UDP Proxy
*********

For UDP Proxy, it is possible to enable periodic logging by configuring an
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.UdpAccessLogOptions.access_log_flush_interval>`

TCP Proxy
*********

For TCP Proxy, it is possible to enable periodic logging by configuring an
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TcpAccessLogOptions.access_log_flush_interval>`

.. note::
   The first log entry is generated one interval after a new connection is received, regardless of whether an upstream
   connection is made.

HTTP Connection Manager
***********************

For HTTP Connection Manager, it is possible to enable periodic logging by configuring an
:ref:`access log flush interval <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.HcmAccessLogOptions.access_log_flush_interval>`

.. note::
   The first log entry is generated one interval after a new HTTP request is received by the HTTP Connection Manager
   (and before processing the filter chain), regardless of whether an upstream connection is made.

HTTP Router Filter
******************

For Router Filter, it is possible to enable periodic logging by configuring an
:ref:`upstream log flush interval <envoy_v3_api_field_extensions.filters.http.router.v3.Router.UpstreamAccessLogOptions.upstream_log_flush_interval>`

.. note::
   The first log entry is generated one interval after a new HTTP request is received by the router filter, regardless
   of whether an upstream connection is made.

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

* Uses an asynchronous I/O flushing mechanism so it never blocks the main network threads.
* Offers customizable log formats through predefined fields and arbitrary HTTP request/response headers.

gRPC
****

* Used to send access log messages to a gRPC access logging service.

Stdout
*********

* Uses an asynchronous I/O flushing mechanism so it never blocks the main network threads.
* Offers customizable log formats through predefined fields and arbitrary HTTP request/response headers.
* Writes to the standard output of the process. It is supported on all platforms.

Stderr
********

* Uses an asynchronous I/O flushing mechanism so it never blocks the main network threads.
* Offers customizable log formats through predefined fields and arbitrary HTTP request/response headers.
* Writes to the standard error of the process. It is supported on all platforms.

Fluentd
********

* Sends access logs over a TCP connection to an upstream destination that supports the Fluentd Forward Protocol as described in:
  `Fluentd Forward Protocol Specification <https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1>`_.
* The data sent over the wire is a stream of
  `Fluentd Forward Mode events <https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1#forward-mode>`_
  which may contain one or more access log entries (depending on the flushing interval and other configuration parameters).

Further reading
---------------

* Access log :ref:`configuration <config_access_log>`.
* File :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.file.v3.FileAccessLog>`.
* gRPC :ref:`Access Log Service (ALS) <envoy_v3_api_msg_extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig>`
  sink.
* OpenTelemetry (gRPC) :ref:`LogsService <envoy_v3_api_msg_extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig>`
* Stdout :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StdoutAccessLog>`
* Stderr :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StderrAccessLog>`
* Fluentd :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.fluentd.v3.FluentdAccessLogConfig>`
