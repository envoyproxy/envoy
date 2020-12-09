.. _statistics:

统计
==========

.. _server_statistics:

Server
------

Server 相关的统计以 *server.* 为根，统计信息如下:

.. csv-table::
  :header: 名字, 类型, 描述
  :widths: 1, 1, 2

  uptime, 观测值, 当前 server 运行时间，以秒为单位
  concurrency, 观测值, 工作线程数
  memory_allocated, 观测值, 当前分配的内存总字节。在热重启时是，新、旧两 Envoy 进程的总量。
  memory_heap_size, 观测值, 当前预留的堆大小总字节。在热重启时是新 Envoy 进程的堆大小。
  memory_physical_size, 观测值, 当前估计物理内存总字节. 在热重启时是新 Envoy 进程的物理内存大小。
  live, 观测值, "1 表示当前 server 没有被耗尽, 否则为 0"
  state, 观测值, 当前 server 的 :ref:`状态 <envoy_v3_api_field_admin.v3.ServerInfo.state>` 。
  parent_connections, 观测值, 热重启时旧 Envoy 进程的全部连接数。
  total_connections, 观测值, 新，旧 Envoy进程的全部连接数
  version, 观测值, 一整型值表示的基于 SCM 修订的版本号或是已设置的 :ref:`stats_server_version_override <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_server_version_override>` 。
  days_until_first_cert_expiring, 观测值, 下一个证书到期的天数
  seconds_until_first_ocsp_response_expiring, 观测值, 下一个 OCSP 响应到期的秒数
  hot_restart_epoch, 观测值, 当前热重启的纪元 -- 一个通过命令行参数 `--restart-epoch` 传递的整型值，通常指代。
  hot_restart_generation, 观测值, 但前热重启的代数 -- 类似 hot_restart_epoch 但是通过递增上一代自动计算得来的。
  initialization_time_ms, 直方图, Envoy 初始化花费的全部时间，以毫秒为单位。 它是从 server 启动直到工作线程准备好接受新连接的时间
  debug_assertion_failures, 计数器, 如果带了编译参数 `--define log_debug_assert_in_release=enabled` 则表示在一个发布版本中检测到调试断言失败的次数，否则为0
  envoy_bug_failures, 计数器, 在一个发布版本中检测到的 Envoy bug 失败的数目。如果这是一个严重的问题请文件或报告这个issue.
  static_unknown_fields, 计数器, 静态配置中具有未知字段的消息数
  dynamic_unknown_fields, 计数器, 动态配置中具有未知字段的消息数

