管理服务器
-----------

.. _config_overview_mgmt_con_issues:

管理服务器无法访问
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

当 Envoy 实例与管理服务器失去连接时，Envoy 将锁定先前的配置，同时在后台积极重试以重新建立与管理服务器的连接。

重要的是，Envoy 会探测与管理服务器的连接是否健康，以便于它可以尝试建立新的连接。建议在连接到管理服务器的集群中配置
:ref:`TCP keep-alives <envoy_v3_api_field_config.cluster.v3.UpstreamConnectionOptions.tcp_keepalive>`
或 :ref:`HTTP/2 keepalives <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.connection_keepalive>`
保持活动状态。

Envoy 调试模式会如实记录它无法与管理服务器建立连接时每次尝试连接的日志。

:ref:`connected_state <management_server_stats>` 统计提供了用于监控此行为的信号。

.. _management_server_stats:

统计
^^^^^^^^^^

管理服务器有一个以 *control_plane* 为根的统计树。统计信息如下：

.. csv-table::
   :header: 名字, 类型, 描述
   :widths: 1, 1, 2

   connected_state, Gauge, 一个布尔值（1用于连接，0用于断开连接），表示与管理服务器的当前连接状态
   rate_limit_enforced, Counter, 被管理服务器强制执行速率限制的总次数
   pending_requests, Gauge, 实施速率限制时的待处理请求总数
   identifier, TextReadout, 发送上一个发现响应的控制平面实例的标识符

.. _subscription_statistics:

xDS 订阅统计
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy 通过被称为 *xDS* 的发现服务来发现其各种动态资源。通过 :ref:`订阅 <xds_protocol>` 请求的资源，
通过指定的文件系统路径来监视、初始化 gRPC 流或轮询 REST-JSON URL。

将为所有订阅生成以下统计信息。

.. csv-table::
 :header: 名字, 类型, 描述
 :widths: 1, 1, 2

 config_reload, Counter, 由于配置不同而导致重新加载配置的总 API 获取次数
 init_fetch_timeout, Counter, 总 :ref:`初始提取超时 <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`
 update_attempt, Counter, 尝试获取的 API 总数
 update_success, Counter, 获取成功的 API 总数
 update_failure, Counter, 由于网络错误而导致 API 获取失败的总数
 update_rejected, Counter, 由于 schema/validation 错误而导致 API 获取失败的总数
 update_time, Gauge, 上一次尝试获取成功的 API 的时间戳，以毫秒为单位。即使在不包含任何配置更改的配置重载后也会刷新。
 version, Gauge, 上一次成功获取 API 的内容的哈希值
 version_text, TextReadout, 上一次成功获取 API 的版本文本
 control_plane.connected_state, Gauge, 布尔值（1表示已连接，0表示已断开连接），表示与管理服务器的当前连接状态
