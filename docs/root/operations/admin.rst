.. _operations_admin_interface:

管理接口
============

Envoy 公开了一个本地管理界面，该界面可用于查询和修改服务器的不同方面：

* :ref:`v3 API reference <envoy_v3_api_msg_config.bootstrap.v3.Admin>`

.. _operations_admin_interface_security:

.. attention::

  当前形式的管理界面不仅允许执行破坏性操作（例如，关闭服务器），而且还可能公开私有信息（例如，统计信息、集群名称、证书信息等）。
  **至关重要的是**，仅允许通过安全网络访问管理界面。将访问管理界面的主机 **仅** 连接到安全网络也很 **重要**（即，避免 CSRF 攻击）。
  这涉及设置适当的防火墙，或者最好只允许通过本地主机访问管理监听器。可以使用以下 v2 配置来完成此操作：

  .. code-block:: yaml

    admin:
      access_log_path: /tmp/admin_access.log
      profile_path: /tmp/envoy.prof
      address:
        socket_address: { address: 127.0.0.1, port_value: 9901 }

  将来，其他安全选项将添加到管理接口。在 `此 <https://github.com/envoyproxy/envoy/issues/2763>`_ issue 中跟踪了这项工作。
  
  所有变更都必须作为 HTTP POST 操作发送。通过 GET 请求变更时，该请求无效，并返回 HTTP 400（无效请求）响应。

.. note::

  对带有 *?format=json* 的断点，它将转储数据作为 JSON 序列化原型。具有默认值的字段不会呈现。例如 */clusters?format=json*，
  当其值为 :ref:`DEFAULT priority <envoy_v3_api_enum_value_config.core.v3.RoutingPriority.DEFAULT>` 时，将断路器阈值优先级字段，如下所示：

  .. code-block:: json

    {
     "thresholds": [
      {
       "max_connections": 1,
       "max_pending_requests": 1024,
       "max_requests": 1024,
       "max_retries": 1
      },
      {
       "priority": "HIGH",
       "max_connections": 1,
       "max_pending_requests": 1024,
       "max_requests": 1024,
       "max_retries": 1
      }
     ]
    }

.. http:get:: /

  呈现 HTML 主页，其中包含指向所有可用选项链接的表。

.. http:get:: /help

  打印所有可用选项的文本表。

.. _operations_admin_interface_certs:

.. http:get:: /certs

  列出所有已加载的 TLS 证书，包括文件名、序列号、主题备用名称以及 JSON 格式的到期天数，这些声明均符合 :ref:`证书原型定义 <envoy_v3_api_msg_admin.v3.Certificates>`。

.. _operations_admin_interface_clusters:

.. http:get:: /clusters

  列出所有已配置的 :ref:`集群管理器 <arch_overview_cluster_manager>` 集群。该信息包括每个集群中所有发现的上游主机以及每个主机的统计信息。
  这对于调试服务发现问题很有用。

  集群管理器信息
    - ``version_info`` 字符串 -- 最后加载的 :ref:`CDS<config_cluster_manager_cds>` 更新的版本信息字符串。
      如果 Envoy 没有 :ref:`CDS<config_cluster_manager_cds>` 设置，则输出将显示 ``version_info::static``。

  集群范围信息
    - 所有优先级设置的 :ref:`断路器 <config_cluster_manager_cluster_circuit_breakers>` 设置。

    - 有关安装了检测器的 :ref:`异常检测 <arch_overview_outlier_detection>` 的信息。目前，
      会展示 :ref:`平均成功率 <envoy_v3_api_field_data.cluster.v3.OutlierEjectSuccessRate.cluster_average_success_rate>` 和 
      :ref:`弹出阈值 <envoy_v3_api_field_data.cluster.v3.OutlierEjectSuccessRate.cluster_success_rate_ejection_threshold>`。
      如果最后一次 :ref/`interval<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>` 中没有足够的数据来计算它们，那么这两个值都可能为 ``-1``。

    - ``added_via_api`` 标识 -- 假如集群是通过静态配置添加的，为 ``false``。如果集群是通过 :ref:`CDS<config_cluster_manager_cds>` api 添加的，则为 ``true``。

  各主机统计
    .. csv-table::
      :header: Name, Type, Description
      :widths: 1, 1, 2

      cx_total, Counter, 总连接数
      cx_active, Gauge, 活跃连接数
      cx_connect_fail, Counter, 连接总数
      rq_total, Counter, 总请求数
      rq_timeout, Counter, 超时请求数
      rq_success, Counter, 非 5xx 响应请求数
      rq_error, Counter, 5xx 响应请求数
      rq_active, Gauge, 活跃请求
      healthy, String, 主机的健康状态。见下文
      weight, Integer, 负载均衡权重（1-100）
      zone, String, 服务域
      canary, Boolean, 主机是否是金丝雀发布
      success_rate, Double, “请求成功率（0-100），如果在 :ref:`时间间隔<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>` 内
      没有足够的 :ref:`请求量 <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>`用于计算，则为 -1。”

  主机健康状态
    主机由于一个或多个不同的失败健康状态而处于是健康或者不健康状态。如果主机健康，则 ``healthy`` 属于将等同于 *健康*

    如果主机是不健康的，则 ``healthy`` 输出由一个或者多个如下字符串组成：

    */failed_active_hc*: 主机无法通过 :ref:`活动健康检查 <config_cluster_manager_cluster_hc>`。

    */failed_eds_health*: 主机被 EDS 标记为不健康。

    */failed_outlier_check*: 主机未完成异常值检测检查。

.. http:get:: /clusters?format=json

  将 */clusters* 以 JSON 序列化原型转储输出。见 :ref:`定义 <envoy_v3_api_msg_admin.v3.Clusters>` 了解更多信息。

.. _operations_admin_interface_config_dump:

.. http:get:: /config_dump

  将当前从 Envoy 组件中加载的配置转储为 JSON 序列化的原型消息。有关更多信息，
  见 :ref:`响应定义 <envoy_v3_api_msg_admin.v3.ConfigDump>`。

.. warning::
  配置可以包括 :ref:`TLS 证书 <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`。
  转储配置前，Envoy 将尝试编辑找到的任何证书的 ``private_key`` 和 ``password`` 字段。这取决于配置是否为强类型 protobuf 消息。
  如果 Envoy 配置使用已弃用的 ``config`` 字段（类型 ``google.protobuf.Struct``），请更新为建议的 ``typed_config`` 字段（类型
   ``google.protobuf.Any``）以确保正确编辑敏感数据。

.. warning::
  基础原型被标记为 v2alpha，因此不能保证其内容（包括 JSON 表示形式）是稳定的。

.. _operations_admin_interface_config_dump_include_eds:

.. http:get:: /config_dump?include_eds

  转储当前加载的配置（包括EDS）。有关更多信息，见 :ref:`响应定义 <envoy_v3_api_msg_admin.v3.EndpointsConfigDump>`。

.. _operations_admin_interface_config_dump_by_mask:

.. http:get:: /config_dump?mask={}

  指定要返回的字段的子集。掩码被解析为 ``ProtobufWkt::FieldMask`` 并应用于每个顶级转储，例如
  :ref:`BootstrapConfigDump <envoy_v3_api_msg_admin.v3.BootstrapConfigDump>` 和
  :ref:`ClustersConfigDump <envoy_v3_api_msg_admin.v3.ClustersConfigDump>`。如果同时指定了资源和掩码查询参数，
  则此行为将更改。详情请见下文。

.. _operations_admin_interface_config_dump_by_resource:

.. http:get:: /config_dump?resource={}

  仅转储与指定资源匹配的已加载的配置。资源必须是顶级配置转储之一中的重复字段，例如 
  :ref:`ListenersConfigDump <envoy_v3_api_msg_admin.v3.ListenersConfigDump>` 中的 
  :ref:`static_listeners <envoy_v3_api_field_admin.v3.ListenersConfigDump.static_listeners>`，
  或者 :ref:`ClustersConfigDump <envoy_v3_api_msg_admin.v3.ClustersConfigDump>` 中的
  :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>`。
  如果需要非重复字段，请使用上面的掩码查询参数。如果只需要重复资源的字段的子集，请按照一下两个说明使用。



.. _operations_admin_interface_config_dump_by_resource_and_mask:

.. http:get:: /config_dump?resource={}&mask={}

  当同时指定资源和掩码查询参数，会将掩码应用与所需的重复字段中的每个元素，以便仅返回字段的自己。掩码被解析为 ``ProtobufWkt::FieldMask``。

  例如，使用 ``/config_dump?resource=dynamic_active_clusters&mask=cluster.name`` 获取所有活跃的动态集群的名字

.. http:get:: /contention

  假如启用了互斥跟踪，则以 JSON 格式转储当前 Envoy 互斥争用统计信息（:ref:`MutexStats <envoy_v3_api_msg_admin.v3.MutexStats>`）。
  见 :option:`--enable-mutex-tracing`。

.. http:post:: /cpuprofiler

  启用或禁用 CPU 事件探查器。需要使用 gperftools 进行编译。可以通过 admin.profile_path 配置输出文件。

.. http:post:: /heapprofiler

  启用或禁用堆分析器。需要使用 gperftools 进行编译。可以通过 admin.profile_path 配置输出文件。

.. _operations_admin_interface_healthcheck_fail:

.. http:post:: /healthcheck/fail

  入站健康检查失败。这需要使用HTTP :ref:`健康检查过滤器 <config_http_filters_health_check>`。
  这对于在关闭服务器或完全重新启动之前排空服务器很有用。无论过滤器的配置方式如何（通过等），调用此命令通常都会使健康检查请求失败。

.. _operations_admin_interface_healthcheck_ok:

.. http:post:: /healthcheck/ok

  消除 :http:post:`/healthcheck/fail` 的影响。这需要使用 HTTP :ref:`健康检查过滤器 <config_http_filters_health_check>`。

.. http:get:: /hot_restart_version

  See :option:`--hot-restart-version`.

.. _operations_admin_interface_init_dump:

.. http:get:: /init_dump

  将各种 Envoy 组件未就绪目标的当前信息转储为 JSON 序列化的原始消息。
  有关更多信息，见 :ref:`响应定义 <envoy_v3_api_msg_admin.v3.UnreadyTargetsDumps>`。

.. _operations_admin_interface_init_dump_by_mask:

.. http:get:: /init_dump?mask={}

  指定掩码查询参数时，掩码值是转储未就绪目标的所需组件。掩码被解析为 ``ProtobufWkt::FieldMask``。

  例如，通过 ``/init_dump?mask=listener`` 获取为所有监听器中未就绪的目标。

.. _operations_admin_interface_listeners:

.. http:get:: /listeners

  列出所有配置的 :ref:`监听器 <arch_overview_listeners>`。此信息包括监听器的名称以及正在监听的地址。
  如果将监听器配置为监听端口 0，则输出将包含实际的操作系统分配的端口。

.. http:get:: /listeners?format=json

  将 */listeners* 输出转储到 JSON 序列化的原型中。详细信息见 :ref:`定义 <envoy_v3_api_msg_admin.v3.Listeners>`。

.. _operations_admin_interface_logging:

.. http:post:: /logging

  在特定的记录器或者所有记录器上启用/禁用不同的日志记录级别。

  - 要更改所有记录器的日志记录级别，将查询参数设置为 level=<期望级别>。
  - 要修改特定记录器的级别，将查询参数设置为 <logger_name>=<期望级别>。
  - 要列出所有的记录器，发送 POST 请求到 /logging 断点，而不带查询参数。

  .. note::

    通常仅在开发期间使用。设置为 `--enable-fine-grain-logging` 后，
    记录器由其所属文件的路径表示（具体来说，由 `__FILE__` 确定的路径），
    因此记录器列表将显示一个文件路径列表，并且特定路径应用作 <logger_name > 来更改日志级别。

.. http:get:: /memory

  打印当前的内存分配/堆使用情况。代替打印所有 `/stats` 和过滤以获取与内存相关的统计信息很有用。

.. http:post:: /quitquitquit

  赶紧利落地退出服务器。

.. http:post:: /reset_counters

  重置所有计数器为零。在调试过程中，与 :http:get:`/stats` 结合使用很有用。 注意这不会删除任何发送到 statsd 的数据。
  它只会影响本地 :http:get:`/stats` 指令的输出。

.. _operations_admin_interface_drain:

.. http:post:: /drain_listeners

   :ref:`排空 <arch_overview_draining>` 所有监听器

   .. http:post:: /drain_listeners?inboundonly

   :ref:`排空 <arch_overview_draining>` 所有入站监听器。:ref:`监听器 <envoy_v3_api_msg_config.listener.v3.Listener>` 
   中的 `traffic_direction` 字段用来判断监听器是入站的还是出站的。

   .. http:post:: /drain_listeners?graceful

   排空监听器时，在关闭监听器前进入一个优雅的排空期。这种行为和持续时间可通过服务器选项或者 CLI（:option:`--drain-time-s` and :option:`--drain-strategy`）来配置。

.. attention::

   此操作直接停止工作程序上的匹配监听器。一旦停止了给定流量方向上的监听器，就不允许在该方向上添加和修改监听器。

.. http:get:: /server_info

  输出包含有关正在运行的服务器信息的 JSON 消息。

  示例输出如下所示：

  .. code-block:: json

    {
      "version": "b050513e840aa939a01f89b07c162f00ab3150eb/1.9.0-dev/Modified/DEBUG",
      "state": "LIVE",
      "command_line_options": {
        "base_id": "0",
        "concurrency": 8,
        "config_path": "config.yaml",
        "config_yaml": "",
        "allow_unknown_static_fields": false,
        "admin_address_path": "",
        "local_address_ip_version": "v4",
        "log_level": "info",
        "component_log_level": "",
        "log_format": "[%Y-%m-%d %T.%e][%t][%l][%n] %v",
        "log_path": "",
        "hot_restart_version": false,
        "service_cluster": "",
        "service_node": "",
        "service_zone": "",
        "mode": "Serve",
        "disable_hot_restart": false,
        "enable_mutex_tracing": false,
        "restart_epoch": 0,
        "file_flush_interval": "10s",
        "drain_time": "600s",
        "parent_shutdown_time": "900s",
        "cpuset_threads": false
      },
      "uptime_current_epoch": "6s",
      "uptime_all_epochs": "6s",
      "node": {
        "id": "node1",
        "cluster": "cluster1",
        "user_agent_name": "envoy",
        "user_agent_build_version": {
          "version": {
            "major_number": 1,
            "minor_number": 15,
            "patch": 0
          }
        },
        "metadata": {},
        "extensions": [],
        "client_features": [],
        "listening_addresses": []
      }
    }

  见 :ref:`ServerInfo 原型 <envoy_v3_api_msg_admin.v3.ServerInfo>` 中对输出的解释。

.. http:get:: /ready

  输出反映服务器状态的字符串和错误代码。LIVE 状态返回 200，否则为 503。这个可以用作就绪检查。

  示例输出：

  .. code-block:: none

    LIVE

  查看 :ref:`ServerInfo proto <envoy_v3_api_msg_admin.v3.ServerInfo>` 中 `stat` 字段的输出说明。

.. _operations_admin_interface_stats:

.. http:get:: /stats

  根据需要输出所有统计信息。此命令对本地调试非常有用。直方图将输出计算的分位数，即 P0、P25、P50、P75、P90、P99、P99.9 和 P100.
  每个分位数的输出将采用（间隔，累计值）的形式，其中间隔值代表自上次刷新间隔依赖的摘要，累计值代表自 Envoy 实例启动依赖的摘要。
  直方图输出中的“未记录的值”标识尚未使用值进行更新。
  有关更多信息，见 :ref:`这里 <operations_stats>`。

  .. http:get:: /stats?usedonly

  输出 Envoy 已更新的统计信息（计数器至少增加一次、计量器至少改变一次、直方图至少增加一次）。

  .. http:get:: /stats?filter=regex

  将返回的统计信息过滤为名称与正则表达式 `regex` 匹配的统计信息。与 `usedonly` 兼容。
  默认情况下执行部分匹配，因此 `/stats?filter=server` 将返回所有包含单词 `server` 的统计信息。
  可以使用开始和结束行锚指定全字符串匹配。（例如 `/stats?filter=server`）

.. http:get:: /stats?format=json

  以 JSON 格式输出 /stats。这可以用于以编程的方式访问统计信息。计数器和计量器将采用一组（名称，值）对的形式。
  直方图将在元素“histograms”下，该元素包含“supported_quantiles”（列出支持的分位数）
  和一个 computed_quantiles 数组，该数组具有每个直方图的已计算分位数。

  如果在间隔内直方图未更新，则所有分位数的输出都将为空。

  实例直方图输出如下：

  .. code-block:: json

    {
      "histograms": {
        "supported_quantiles": [
          0, 25, 50, 75, 90, 95, 99, 99.9, 100
        ],
        "computed_quantiles": [
          {
            "name": "cluster.external_auth_cluster.upstream_cx_length_ms",
            "values": [
              {"interval": 0, "cumulative": 0},
              {"interval": 0, "cumulative": 0},
              {"interval": 1.0435787, "cumulative": 1.0435787},
              {"interval": 1.0941565, "cumulative": 1.0941565},
              {"interval": 2.0860023, "cumulative": 2.0860023},
              {"interval": 3.0665233, "cumulative": 3.0665233},
              {"interval": 6.046609, "cumulative": 6.046609},
              {"interval": 229.57333,"cumulative": 229.57333},
              {"interval": 260,"cumulative": 260}
            ]
          },
          {
            "name": "http.admin.downstream_rq_time",
            "values": [
              {"interval": null, "cumulative": 0},
              {"interval": null, "cumulative": 0},
              {"interval": null, "cumulative": 1.0435787},
              {"interval": null, "cumulative": 1.0941565},
              {"interval": null, "cumulative": 2.0860023},
              {"interval": null, "cumulative": 3.0665233},
              {"interval": null, "cumulative": 6.046609},
              {"interval": null, "cumulative": 229.57333},
              {"interval": null, "cumulative": 260}
            ]
          }
        ]
      }
    }

  .. http:get:: /stats?format=json&usedonly

  以 JSON 格式输出 Envoy 已更新的统计信息（计数器至少增加一次、计量器至少改变一次、直方图至少增加一次）。

.. http:get:: /stats?format=prometheus

  或者，

  .. http:get:: /stats/prometheus

  以 `Prometheus <https://prometheus.io/docs/instrumenting/exposition_formats/>`_ v0.0.4 格式输出 /stats。
  这可以用来与 Prometheus 服务器集成。

  可以选择传递 `usedonly` URL 查询参数，来仅获取 Envoy 已更新的统计信息（计数器至少增加一次、计量器至少改变一次、直方图至少增加一次）。

  .. http:get:: /stats/recentlookups

  此端点可以帮助 Envoy 开发人员调试统计系统中潜在的争用问题。最初，仅累加 StatName 查找的计数，
  而不累加正在查到的特定名称。为了查看特定的近期请求，必须通过 POST 到 `/stats/recentlookups/enable` 
  来启用该功能。每次查找可能会增加大约 40-100 纳秒的开销。

  启用后，此端点将发出最近被 Envoy 以字符串形式访问的指标名。理想情况下，仅在启动过程中或通过 xDS 接收新配置时，
  才应将字符串转换为 StatName、计数器、计量器和直方图。这是因为当将统计信息作为字符串查找时，它们必须讲全局符号表锁定。
  在启动过程中这是可以接受的，但是在高核心数计算机上响应的用户请求，由于互斥锁争用，这可能会导致性能问题。

  该管理端点需要 Envoy 使用 `--use-fake-symbol-table 0` 选项启动。

  更多详细信息见 :repo:`source/docs/stats.md`。

  同样要注意互斥征用可以通过 :http:get:`/contention` 进行追踪。

  .. http:post:: /stats/recentlookups/enable

  通过开启 `/stats/recentlookups`，来打开对最近查找的统计名称的收集。

  更多详细信息见 :repo:`source/docs/stats.md`。

  .. http:post:: /stats/recentlookups/disable

  禁用 `/stats/recentlookups` 可关闭对最近查找的统计名称的收集。同时清空查找列表。
  但是统计信息中 `server.stats_recent_lookups` 的计数仍可见，不会被清空且继续累积。

  更多详细信息见 :repo:`source/docs/stats.md`。

  .. http:post:: /stats/recentlookups/clear

  清除所有未完成的查找和技术。同样会清空查找数据以及计数，但是如果启用之后会继续收集。

  更多详细信息见 :repo:`source/docs/stats.md`。

.. _operations_admin_interface_runtime:

.. http:get:: /runtime

  根据需要以 JSON 格式输出所有运行时值。有关如何配置和利用这些值的更多信息，见 :ref:`此处 <arch_overview_runtime>`。
  输出包括活跃运行时覆盖层的列表以及每个键的层值堆栈。空字符串表示没有值，并且堆栈中的最终活动值也包含在单独的键中。
  输出示例：

.. code-block:: json

  {
    "layers": [
      "disk",
      "override",
      "admin",
    ],
    "entries": {
      "my_key": {
        "layer_values": [
          "my_disk_value",
          "",
          ""
        ],
        "final_value": "my_disk_value"
      },
      "my_second_key": {
        "layer_values": [
          "my_second_disk_value",
          "my_disk_override_value",
          "my_admin_override_value"
        ],
        "final_value": "my_admin_override_value"
      }
    }
  }

.. _operations_admin_interface_runtime_modify:

.. http:post:: /runtime_modify?key1=value1&key2=value2&keyN=valueN

  添加或修改在查询参数中传递的运行时值。要删除先前添加的键，请使用一个空字符串作为值。请注意，删除仅适用于通过此端点添加的替代；
  从磁盘加载的值可以通过覆盖进行修改，但不能删除。

.. attention::

  使用 /runtime_modify 端点时要小心。变更会立即生效。
  **至关重要的是**，管理接口必须是 :ref:`正确保护的 <operations_admin_interface_security>`。

  .. _operations_admin_interface_hystrix_event_stream:

.. http:get:: /hystrix_event_stream

  该端点旨在用作 `Hystrix 仪表盘 <https://github.com/Netflix-Skunkworks/hystrix-dashboard/wiki>`_ 的流源。
  到此端点的 GET 请求会触发来自 Envoy `text/event-stream <https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events>`_ 格式的统计数据流，如 Hystrix 仪表板所期望的那样。

  如果从浏览器或终端调用，则响应将显示为连续流，并按由 :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` :ref:`stats_flush_interval <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_flush_interval>`定义的间隔发送。

  仅当在配置文件中启用 Hystrix 接收器时，才启用此处理器，如 :ref:`这里 <envoy_v3_api_msg_config.metrics.v3.HystrixSink>` 记录的那样。

  由于 Envoy 和 Hystrix 的弹性机制不同，因此必须调整仪表板中显示的某些统计信息：

  * ** 线程池拒绝 ** - 通常类似于 Envoy 中的断路，并由 *upstream_rq_pending_overflow* 进行计数，
    尽管术语线程池不适用于 Envoy。在 Hystrix 和 Envoy 中，结果都是被拒绝的请求没有传递到上游。
  * ** 断路器状态（闭合或断开）** - 由于在 Envoy 中，基于队列中当前连接/请求的数量断开了电路，因此断路器没有睡眠窗口，
    因此断开/闭合是瞬时的。因此，我们将断路器状态设置为“强制闭合”。
  * ** 短接（拒绝）** - 该属于存在于 Envoy 中，但指的是由于超出限制（队列或者连接）而未返送的请求，而在 Hystrix 中，
    指的是在某个时间范围内，由于服务不可用响应的百分比很高而未发送的请求。在 Envoy 中，服务不可用响应将导致“异常检测” - 
    从负载均衡器池中移除该节点，但最终不会拒绝请求。因此，这个计数器始终设置为 ‘0’。
  * 延迟信息表示自上次刷新以来的数据。
    平均延迟目前不可用。

.. http:post:: /tap

  该端点用于配置活动的 tap 会话。仅当配置了有效的 tag 扩展并改扩展配置为接受管理员配置时，此功能才启用。。见：

  * :ref:`HTTP tap 过滤器配置 <config_http_filters_tap_admin_handler>`

.. http:post:: /reopen_logs

  触发重新打开所有访问日志。行为类似 SIGUSR1 的处理。
