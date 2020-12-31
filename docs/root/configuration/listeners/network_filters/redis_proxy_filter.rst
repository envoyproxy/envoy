.. _config_network_filters_redis_proxy:

Redis 代理
===========

* Redis :ref:`架构概览 <arch_overview_redis>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.redis_proxy.v3.RedisProxy>`
* 过滤器的名称应该配置为 *envoy.filters.network.redis_proxy* 。

.. _config_network_filters_redis_proxy_stats:

统计信息
----------

每个配置的 Redis 代理过滤器都有以 *redis.<stat_prefix>.* 为根如下所示的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  downstream_cx_active, Gauge, 活跃连接总数
  downstream_cx_protocol_error, Counter, 协议异常数
  downstream_cx_rx_bytes_buffered, Gauge, 当前缓冲中接收的字节数
  downstream_cx_rx_bytes_total, Counter, 接收的字节数
  downstream_cx_total, Counter, 总连接数
  downstream_cx_tx_bytes_buffered, Gauge, 当前缓冲中发送的字节
  downstream_cx_tx_bytes_total, Counter, 发送的字节数
  downstream_cx_drain_close, Counter, 由于排空而关闭的连接数
  downstream_rq_active, Gauge, 活跃的请求数
  downstream_rq_total, Counter, 请求数


拆分器（Splitter）统计信息
-----------------------------

Redis 过滤器将使用以下所示统计信息收集 *redis.<stat_prefix>.splitter.* 中命令拆分器的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  invalid_request, Counter, 参数数目不正确的请求数
  unsupported_command, Counter, 命令拆分器无法识别发出的命令数

命令统计信息
----------------------

Redis 过滤器将会收集 *redis.<stat_prefix>.command.<command>.* 命名空间中命令的统计信息。默认情况下延迟信息以毫秒为单位，并且可以通过设置配置参数 :ref:`latency_in_micros <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.latency_in_micros>` 为 true 更改为微秒。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  total, Counter, 命令数
  success, Counter, 成功命令数
  error, Counter, 返回部分或完整异常响应的命令数
  latency, Histogram, 命令执行时间毫秒（包含延迟故障）
  error_fault, Counter, 注入异常故障的命令数
  delay_fault, Counter, 注入延迟故障的命令数
  
.. _config_network_filters_redis_proxy_per_command_stats:

运行时
-------

Redis 代理过滤器支持如下所示的运行时设置：

redis.drain_close_enabled
  连接的百分比表示如果服务器正在关闭，则将关闭连接，否则将尝试关闭连接。默认为 100。

.. _config_network_filters_redis_proxy_fault_injection:

故障注入
---------------

Redis 过滤器可以执行故障注入。当前，延迟和异常故障都是支持的。延迟故障延迟请求，异常故障以异常响应。此外，异常也可以延迟。

注意，Redis 过滤器不会检查配置的正确性，因为这是用户负责确保默认值和运行时百分比都是正确的。这是因为百分比可以在运行时被修改，同时在请求时验证正确性是非常昂贵的。
如果指定了多个故障，对于默认的故障和 Redis 命令组合注入故障百分比之和不会超过 100%。例如，如果指定了两个故障，一个应用于 60% 的 GET 请求，另一个应用于所有命令的 50%，这显然是一个错误的配置，因为 GET 请求有 110% 的机会应用错误。
这也就意味每个请求都会有一个错误。

如果注入一个延迟，那么延迟是会累加的。如果请求花费 400ms 再注入 100ms 的延迟，那么总的请求延迟就是 500ms。此外，由于 Redis 协议的实施，因为代理需要维护它所收到的命令顺序，所以延迟请求将会延迟在他之后的所有请求。

注意，故障必须有一个 `fault_enabled` 字段，并且默认情况下是不启用的（如果没有默认值或在运行时设置值）。

配置示例：

.. code-block:: yaml

  faults:
  - fault_type: ERROR
    fault_enabled:
      default_value:
        numerator: 10
        denominator: HUNDRED
      runtime_key: "bogus_key"
      commands:
      - GET
    - fault_type: DELAY
      fault_enabled:
        default_value:
          numerator: 10
          denominator: HUNDRED
        runtime_key: "bogus_key"
      delay: 2s

这个示例创建了两个故障，一个应用于 10% 的 GET 请求异常。另一个应用于 10% 的所有命令延迟。这就意味着 20% 的 GET 命令将应用一个错误，如前所述的一样。
