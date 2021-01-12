.. _config_http_filters_fault_injection:

故障注入
===========

故障注入过滤器可用于测试微服务对不同形式故障的恢复能力。过滤器可用于注入延迟并使用用户指定的错误码中止请求，
从而提供暂存不同故障场景的能力，例如服务故障、服务过载、高网络延迟、网络分区等。
故障注入可被限制为一组基于（目标）上游集群请求和/或一组预定义请求头的特殊请求。

故障范围仅限于通过网络通信的应用中可被观察到的那些故障。本地主机上的CPU和磁盘故障无法被模拟。

配置
-----------

.. note::

  故障注入过滤器必须被放在所有的过滤器之前，其中也包含路由过滤器。

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.fault.v3.HTTPFault>`
* 此过滤器的名字应该被配置为 *envoy.filters.http.fault*。

.. _config_http_filters_fault_injection_http_header:

通过 HTTP 头部控制故障注入
--------------------------------------------

故障过滤器有允许调用者指定故障配置的能力。在特定场景中，允许客户端定义自己的故障配置是有用的。
目前被支持的头部控制参数有：

x-envoy-fault-abort-request
  根据 HTTP 状态码中断请求。一个请求返回的响应体头部中的 HTTP 状态码的值应该是一个整数，
  且该整数必须在 [200, 600) 范围内。为确保头部生效，需设置 :ref:`header_abort
  <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort.header_abort>`。

x-envoy-fault-abort-grpc-request
  根据 gRPC 状态码中断请求。一个请求返回的响应体头部中的 gRPC 状态码的值应该是一个非负整数。
  其值范围是 [0, UInt32.Max] 而不是 [0, 16]，甚至允许测试未定义的 gRPC 状态码。当头部设置了 gRPC 状态码，
  HTTP 响应状态码将被设置为 200。为确保头部生效，需设置 :ref:`header_abort
  <envoy_api_field_config.filter.http.fault.v2.FaultAbort.header_abort>`。如果在头部同时设置了
  *x-envoy-fault-abort-request* 和 *x-envoy-fault-abort-grpc-request*，那么头部的
  *x-envoy-fault-abort-grpc-request* 信息将被 **忽略**
  并且故障响应的 HTTP 状态码将被设置为 *x-envoy-fault-abort-request* 的值。

x-envoy-fault-abort-request-percentage
  在头部设置 *x-envoy-fault-abort-request* 或 *x-envoy-fault-abort-grpc-request* 作为状态码控制失败请求的百分比。
  用于指定中断请求的百分比的分子的这个头部值应该是一个整数，且大于或等于 0，该值的最大值由 :ref:`percentage
  <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort.percentage>` 字段中的分子值限制。
  百分比的分母值等于 :ref:`percentage <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort.percentage>`
  字段默认百分比的分母。为确保头部生效，需设置 :ref:`header_abort
  <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort.header_abort>` 且HTTP 头部的
  *x-envoy-fault-abort-request* 或 *x-envoy-fault-abort-grpc-request* 需作为请求的一部分。

x-envoy-fault-delay-request
  请求延迟时长。指定延迟毫秒数的头部值应该是一个整数。为确保头部生效，需设置 :ref:`header_delay
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultDelay.header_delay>`。

x-envoy-fault-delay-request-percentage
  HTTP 头部 *x-envoy-fault-delay-request* 的值定义了延迟请求的百分比。头部用于指定延迟请求的百分比的值应该是一个整数，
  且大于或等于 0，该值的最大值由 :ref:`percentage
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultDelay.percentage>` 字段中的分子值限制。
  百分比的分母值等于 :ref:`percentage <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultDelay.percentage>`
  字段默认百分比的分母。为确保头部生效，需设置 :ref:`header_delay
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultDelay.header_delay>` 且HTTP 头部的
  *x-envoy-fault-delay-request* 需作为请求的一部分。

x-envoy-fault-throughput-response
  用于限制发送给调用方响应的比率。头部指定在 KiB/s 内的限制值应该是一个整数，且必须大于 0。为确保头部生效，
  需设置 :ref:`header_limit <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultRateLimit.header_limit>`。

x-envoy-fault-throughput-response-percentage
  头部 *x-envoy-fault-throughput-response* 的值限制获取响应的请求的百分比。头部指定获取响应的请求的百分比的值应该是一个整数，
  且大于或等于 0，该值的最大值由 :ref:`percentage
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultRateLimit.percentage>` 字段中的分子值限制。
  百分比的分母值等于 :ref:`percentage <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultRateLimit.percentage>`
  字段默认百分比的分母。为确保头部生效，需设置 :ref:`header_limit
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultRateLimit.header_limit>`
  且HTTP 头部的 *x-envoy-fault-delay-request* 需作为请求的一部分。

.. attention::

  本质上，暴露头部控制权限有潜在风险，尤其是被暴露给不受信任的客户端。这时，建议使用 :ref:`max_active_faults
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.max_active_faults>`
  设置去限制任意给定时间内可激活的最大并发故障数。

以下的示例配置启用了上述头部控制参数的选项：

.. code-block:: yaml

  name: envoy.filters.http.fault
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
    max_active_faults: 100
    abort:
      header_abort: {}
      percentage:
        numerator: 100
    delay:
      header_delay: {}
      percentage:
        numerator: 100
    response_rate_limit:
      header_limit: {}
      percentage:
        numerator: 100

.. _config_http_filters_fault_injection_runtime:

运行时
-----------

HTTP 故障注入过滤器支持以下全局运行时设置：

.. attention::

  以下有些运行时的键需要在过滤器的配置中指定故障类型，而有些不需要。每个键的更多信息请参考文档。

fault.http.abort.abort_percent
  头部匹配时，被中断的请求的百分比。默认为配置中指定的 *abort_percent*。如果配置中不包含 *abort* 配置块，
  那么 *abort_percent* 默认为 0。由于历史原因，该运行时键是否可用取决于该过滤器是否 :ref:`配置为中断
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.abort>`。

fault.http.abort.http_status
  头部匹配时，HTTP 状态码将被用于作为中断请求的响应状态码。默认为配置中指定的 HTTP 状态码。如果配置中不包含 *abort* 配置块，
  那么 *http_status* 默认为 0。由于历史原因，该运行时键是否可用取决于该过滤器是否 :ref:`配置为中断
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.abort>`.

fault.http.abort.grpc_status
  头部匹配时，gRPC 状态码将被用于作为中断请求的响应状态码。默认为配置中指定的 gRPC 状态码。
  如果运行时和配置中都缺少此字段，则响应中的 gRPC 状态码将从 *fault.http.abort.http_status* 字段中派生。
  仅当过滤器 :ref:`配置为中断 <envoy_api_field_config.filter.http.fault.v2.HTTPFault.abort>`
  该运行时键才可用。

fault.http.delay.fixed_delay_percent
  头部匹配时，被延迟的请求的百分比。默认为配置中指定的 *delay_percent* 否则为 0。仅当过滤器是 :ref:`配置为延迟
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.delay>` 该运行时键才可用。

fault.http.delay.fixed_duration_ms
  延迟持续时间（以毫秒为单位）。如果未指定，将使用配置中指定的 *fixed_duration_ms*。如果运行时和配置中都缺少此字段，
  则不会有延迟注入。仅当过滤器 :ref:`配置为延迟
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.delay>` 该运行时键才可用。

fault.http.max_active_faults
  Envoy 通过故障过滤器注入的最大激活故障数（含所有类型）。故障可被 100% 注入任何想要使用的场景中，
  但是用户希望避免由于太多的意外并发故障请求引起的资源限制问题。如果没有指定，将使用 :ref:`max_active_faults
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.max_active_faults>` 的设置。

fault.http.rate_limit.response_percent
  已注入响应率限制故障的请求的百分比。默认该值在 :ref:`percentage
  <envoy_v3_api_field_extensions.filters.common.fault.v3.FaultRateLimit.percentage>` 字段中设置。
  仅当过滤器 :ref:`配置为响应率限制
  <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.response_rate_limit>` 该运行时键才可用。

*注意*，如果存在特定的下游集群的故障过滤器运行时设置，默认设置将被覆盖。以下是下游特定运行时键：

* fault.http.<downstream-cluster>.abort.abort_percent
* fault.http.<downstream-cluster>.abort.http_status
* fault.http.<downstream-cluster>.delay.fixed_delay_percent
* fault.http.<downstream-cluster>.delay.fixed_duration_ms

下游集群名称在 :ref:`the HTTP x-envoy-downstream-service-cluster
<config_http_conn_man_headers_downstream-service-cluster>` 头部中获取。如果未在运行时设置中找到该配置，
默认采用全局运行时设置。全局运行时设置是默认配置。

.. _config_http_filters_fault_injection_stats:

统计
-----------

故障过滤器在命名空间 *http.<stat_prefix>.fault.* 输出统计信息。 :ref:`统计前缀
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
来自拥有 HTTP 连接的管理器。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  delays_injected, Counter, 被延迟的请求总数
  aborts_injected, Counter, 被中断的请求总数
  response_rl_injected, Counter, 选择注入响应率限制的请求总数（实际上由于断开连接、重置、没有主体等，注入可能没有发生）
  faults_overflow, Counter, 超过 :ref:`max_active_faults <envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.max_active_faults>` 配置中最大激活故障数后无法被注入的故障总数
  active_faults, Gauge, 当前时间激活故障总数
  <downstream-cluster>.delays_injected, Counter, 给定下游集群的延迟请求总数
  <downstream-cluster>.aborts_injected, Counter, 给定下游集群的中止请求总数
