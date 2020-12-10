.. _arch_overview_access_logs:

访问日志
==============

:ref:`HTTP 连接管理 <arch_overview_http_conn_man>` 与 :ref:`tcp 代理 <arch_overview_tcp_proxy>` 支持可扩展的访问记录，并拥有以下特性：

* 每个连接的上游或下游允许记录任意数量的访问日志。
* 通过自定义的访问日志过滤器可以将不同类型的请求和响应写入到不同的访问日志。

下游连接的访问日志可以通过设置 :ref:`监听器访问日志 <envoy_v3_api_field_config.listener.v3.Listener.access_log>` 启用。监听器访问日志可以补充 HTTP 请求访问日志，并且可以独立于过滤器访问日志启用。

.. _arch_overview_access_log_filters:

访问日志过滤器
---------------

Envoy 支持几种在运行时注册的内置日志过滤器，
:ref:`访问日志过滤器 <envoy_v3_api_msg_config.accesslog.v3.AccessLogFilter>` 和
:ref:`扩展过滤器 <envoy_v3_api_field_config.accesslog.v3.AccessLogFilter.extension_filter>`。

.. _arch_overview_access_logs_sinks:

访问日志接收器
---------------

Envoy 支持可插拔的访问日志接收器. 当前支持的接收器包括：

文件
****

* 采用异步 IO 刷新架构。因此访问日志永远不会阻塞网络处理的主线程。
* 使用预定义字段以及任意 HTTP 请求和响应头的可自定义访问日志格式。

gRPC
****

* Envoy 将访问日志发送到一个支持 gRPC 格式信息的访问日志服务。



进一步阅读
-----------

* 访问日志 :ref:`配置 <config_access_log>`。
* 文件 :ref:`访问日志接收器 <envoy_v3_api_msg_extensions.access_loggers.file.v3.FileAccessLog>`。
* gRPC :ref:`访问日志服务 (ALS) <envoy_v3_api_msg_extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig>` 接收器。
