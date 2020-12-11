.. _statistics:

统计
==========

.. _server_statistics:

服务器
------

服务器相关的统计以 *server.* 为根，统计信息如下:

.. csv-table::
  :header: 名字, 类型, 描述
  :widths: 1, 1, 2

  uptime, Gauge, 当前服务器运行时间，以秒为单位
  concurrency, Gauge, 工作线程数
  memory_allocated, Gauge, 当前分配的内存总字节。在热重启时是，新、旧两 Envoy 进程的总量。
  memory_heap_size, Gauge, 当前预留的堆大小总字节。在热重启时是新 Envoy 进程的堆大小。
  memory_physical_size, Gauge, 当前估计物理内存总字节。 在热重启时是新 Envoy 进程的物理内存大小。
  live, Gauge, "1 表示当前服务器是存活状态, 否则为 0"
  state, Gauge, 当前服务器的 :ref:`状态 <envoy_v3_api_field_admin.v3.ServerInfo.state>` 。
  parent_connections, Gauge, 热重启时旧 Envoy 进程的全部连接数。
  total_connections, Gauge, 新、旧 Envoy进程的全部连接数
  version, Gauge, 一整型值表示的基于 SCM 修订的版本号或是已设置的 :ref:`stats_server_version_override <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_server_version_override>` 。
  days_until_first_cert_expiring, Gauge, 下一个证书到期的天数
  seconds_until_first_ocsp_response_expiring, Gauge, 下一个 OCSP 响应到期的秒数
  hot_restart_epoch, Gauge, 当前热重启的代数 -- 通常由命令行参数 `--restart-epoch` 传递的整数值生成。
  hot_restart_generation, Gauge, 但前热重启的代数 -- 类似 hot_restart_epoch 但是通过递增上一代自动计算得来的。
  initialization_time_ms, Histogram, Envoy 初始化花费的全部时间，以毫秒为单位。 它是从服务器启动直到工作线程准备好接受新连接的时间
  debug_assertion_failures, Counter, 如果带了编译参数 `--define log_debug_assert_in_release=enabled` 则表示在一个发布版本中检测到调试断言失败的次数，否则为 0
  envoy_bug_failures, Counter, 在一个发布版本中检测到的 Envoy bug 故障的数目。如果这是一个严重的问题请提交或报告这个 issue。
  static_unknown_fields, Counter, 静态配置中具有未知字段的消息数
  dynamic_unknown_fields, Counter, 动态配置中具有未知字段的消息数

