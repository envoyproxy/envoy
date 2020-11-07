.. _arch_overview_access_logs:

Access logging
==============

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` and
:ref:`tcp proxy <arch_overview_tcp_proxy>` support extensible access logging with the following
features:

* Any number of access logs per a connection stream.
* Customizable access log filters that allow different types of requests and responses to be written
  to different access logs.

Downstream connection access logging can be enabled using :ref:`listener access
logs<envoy_v3_api_field_config.listener.v3.Listener.access_log>`. The listener access logs complement
HTTP request access logging and can be enabled separately and independently from
filter access logs.

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

Further reading
---------------

* Access log :ref:`configuration <config_access_log>`.
* File :ref:`access log sink <envoy_v3_api_msg_extensions.access_loggers.file.v3.FileAccessLog>`.
* gRPC :ref:`Access Log Service (ALS) <envoy_v3_api_msg_extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig>`
  sink.
