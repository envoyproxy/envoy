.. _operations_cli:

命令行选项
============

Envoy 由 JSON 配置文件以及一组命令行选项驱动。以下是 Envoy 支持的命令行选项。

.. option:: -c <path string>, --config-path <path string>

  *（可选）* v2 :ref:`JSON/YAML/proto3 配置文件 <config>` 的路径。如果缺少此标志，则需要 :option:`--config-yaml`。这将被解析为 :ref:`v2 引导程序配置文件  <config_overview_bootstrap>`。有效的扩展名有 ``.json``、``.yaml``、``.pb`` 和 ``.pb_text``，他们分别表示 JSON、YAML、`binary proto3
  <https://developers.google.com/protocol-buffers/docs/encoding>`_ 和 `text
  proto3
  <https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.text_format>`_ 格式。

.. option:: --config-yaml <yaml string>

  *（可选）* 引导程序配置的 YAML 字符串。如果还设置了:option:`--config-path`，则此 YAML 字符串中的值将覆盖并与从 :option:`--config-path` 路径加载的引导程序配置合并。因为 YAML 是 JSON 的超集，所以 JSON 字符串也可以传递给 :option:`--config-yaml`。

  在命令行中覆盖节点 id 示例：

    .. code-block:: console

      ./envoy -c bootstrap.yaml --config-yaml "node: {id: 'node1'}"

.. option:: --bootstrap-version <integer>

   *（可选）* 加载引导程序所用的 API 版本。该值应为单个整数，例如要将引导程序配置解析为 V3，请指定 ``--bootstrap-version 3``。如果未设置，Envoy 将尝试将引导程序作为前一个 API 版本加载并将其升级到最新版本。如果失败，Envoy 将尝试将配置加载为最新版本。

.. option:: --mode <string>

  *（可选）* Envoy 的操作模式之一：

  * ``serve``：*（默认值）* 验证 JSON 配置，然后正常提供流量。

  * ``validate``：验证 JSON 配置，然后退出，打印“OK”消息（在这种情况下，退出代码为 0）或配置文件生成的任何错误（退出代码 1）。没有生成网络流量，并且不执行热重启过程，因此不会干扰计算机上的其他 Envoy 进程。

.. option:: --admin-address-path <path string>

  *（可选）* 管理地址和端口将被写入的输出文件路径。

.. option:: --local-address-ip-version <string>

  *（可选）* 用于填充服务器本地 IP 地址的 IP 地址版本。此参数影响各种标头，包括附加到 X-Forwarded-For（XFF）标头的标头。选项是``v4`` 或 ``v6``。默认值为 ``v4``。

.. option:: --base-id <integer>

  *（可选）* 分配共享内存区域时要使用的基础 ID。Envoy 在 :ref:`热重启 <arch_overview_hot_restart>` 期间使用共享内存区域。大多数用户将永远不必设置此选项。但是，如果 Envoy 需要在同一台计算机上多次运行，则每个正在运行的 Envoy 都将需要唯一的基础 ID，以便共享内存区域不会发生冲突。

.. option:: --use-dynamic-base-id

  *（可选）* 选择分配共享内存区域时要使用的未使用的基本 ID。但是，最好使用带有 :option:`--base-id` 的预选值。如果启用此选项它将替换 :option:`--base-id` 的值。 当 :option:`--restart-epoch` 的值不为零时，可能不使用此标志。相反，要进行后续的热重启，请使用选定的基本 ID 设置 :option:`--base-id` 选项。见 :option:`--base-id-path`。

.. option:: --base-id-path <path_string>

  *（可选）* 写基本 ID 到给定的路径。尽管该选项与 :option:`--base-id` 兼容，但其预期用户是提供对由 :option:`--use-dynamic-base-id` 选择的动态基本 ID 的访问。

.. option:: --concurrency <integer>

  *（可选）* 要运行的 :ref:`工作线程 <arch_overview_threading>` 数。如果未指定，则默认为计算机上的硬件线程数。如果设置为 0，Envoy 仍将运行一个工作线程。

.. option:: -l <string>, --log-level <string>

  *（可选）* 日志级别。非开发人员通常不应设置该选项。见可用的日志级别和默认日志级别的参考文本。

.. option:: --component-log-level <string>

  *（可选）* 逗号分隔的每个组件的日志记录级别列表。非开发人员通常不应设置此选项。例如，如果希望 `上游` 组件在 `debug` 级别运行，并要在 `trace` 级别运行的 `connection` 组件，则应将 ``upstream:debug,connection:trace`` 传递给此标志。见 :repo:`/source/common/common/logger.h` 中的 ``ALL_LOGGER_IDS`` 以获取组件列表。

.. option:: --cpuset-threads

   *（可选）* 如果未设置 :option:`--concurrency`，则此标志用于控制工作线程的数量。如果启用，则分配的 cpuset 大小用于确定基于 Linux 的系统上的工作线程数。否则，工作线程数将设置为计算机上的硬件线程数。可以在 `内核文档 <https://www.kernel.org/doc/Documentation/cgroup-v1/cpusets.txt>`_ 中阅读有关 cpusets 的更多信息。

.. option:: --log-path <path string>

   *（可选）* 日志应写入的输出文件路径。处理 SIGUSR1 信号时将重新打开此文件。如果未设置，会记录到 stderr。

.. option:: --log-format <format string>

   *（可选）* 用于布局日志消息元数据的格式字符串。如果未设置，将使用默认的格式字符串 ``"[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v"``。

   当与 :option:`--log-format-prefix-with-location` 设置为 1 一起使用时，记录器可以配置为在文件路径和行号前加上 `%v`` 前缀。

   当与 :option:`--log-format-escaped` 一起使用时，可以将记录器配置为以日志查看器可解析的格式记录。:ref:`应用日志配置 <config_application_logs>` 部分中记录了已知的集成。

   支持的格式标志（以及输出示例）：

   :%v:	实际记录的消息（“some user text”）
   :%t:	线程 id（“1232”）
   :%P:	进程 id（“3456”）
   :%n:	记录器名（“filter”）
   :%l:	消息的日志级别（“debug", "info", etc.)
   :%L:	消息的日志级别的缩写（“D", "I", etc.)
   :%a:	星期的缩写（“Tue”）
   :%A:	星期的全名（“Tuesday”）
   :%b:	月份的缩写（“Mar”）
   :%B:	月份的全名（“March”）
   :%c:	日期和时间的展示（“Tue Mar 27 15:25:06 2018”）
   :%C:	2 位数年份（“18”）
   :%Y:	4 位数年份（“2018”）
   :%D, %x:	MM/DD/YY 短日期（“03/27/18”）
   :%m:	月 01-12（“03”）
   :%d:	天01-31（“27”）
   :%H:	24 小时制 00-23（“15”）
   :%I:	12 小时制 01-12（“03”）
   :%M:	分钟 00-59（“25”）
   :%S:	秒 00-59（“06”）
   :%e:	当前秒的毫秒部分 000-999（“008”）
   :%f:	当前秒的微秒部分 000000-999999（“008789”）
   :%F:	当前秒的纳秒部分 000000000-999999999（“008789123”）
   :%p:	AM/PM（“AM”）
   :%r:	12 小时制的时间（“03:25:06 PM”）
   :%R:	24 小时制时间 HH:MM，等同于 %H:%M（“15:25”）
   :%T, %X:	ISO 8601 时间格式（HH:MM:SS），等同于 %H:%M:%S（“13:25:06”）
   :%z:	ISO 8601 与 UTC 时区的偏移量（ISO 8601 offset from UTC in timezone）
   :%%:	% 标志（“%”）
   :%@: 源文件与行号（“my_file.cc:123”）
   :%s: 源文件的基本名称（“my_file.cc”）
   :%g: 源文件的完整相对路径（“/some/dir/my_file.cc”）
   :%#: 源文件行号（“123”）
   :%!: 源方法名（“myFunc”）

.. option:: --log-format-prefix-with-location <1|0>

   *（可选）* 此临时标志允许用 ``"[%g:%#] %v"`` 替换日志格式中``"%v"`` 的所有条目。提供此标志仅用于迁移用途。如果未设置，则使用默认值 0。

   **注意**：在 1.17.0 中这个标志将会移除。

.. option:: --log-format-escaped

  *（可选）* 此标志启用应用程序日志清理以转义 C-style 转义序列。这可用于防止单个日志行跨越基础日志中的多行。这将清除 `此列表 <https://en.cppreference.com/w/cpp/language/escape>`_ 中的所有转义序列。请注意，每行的尾随空白字符（如 EOL 字符）都不会被转义。

.. option:: --restart-epoch <integer>

  *（可选）*：:ref:`热重启 <arch_overview_hot_restart>` 期。（Envoy 已热重启而不是重新启动的次数）。首次启动时默认为 0。此选项告诉 Envoy 是尝试创建热重启所需的共享内存区域，还是打开现有的共享内存区域。每次热重启时都应增加它。:ref:`热重启包装器 <operations_hot_restarter>` 设置在大多数情况下应传递给此选项的 *RESTART_EPOCH* 环境变量。

.. option:: --enable-fine-grain-logging

  *（可选）* 在管理界面上启用具有文件级日志控制和运行时更新的细粒度记录器。如果启用，则主要日志宏包括 `ENVOY_LOG`、`ENVOY_CONN_LOG`、`ENVOY_STREAM_LOG` 和 `ENVOY_FLUSH_LOG` 将使用每个文件的记录器，使用时不再需要 `Envoy::Logger::Loggable`。管理界面的用法与此类似。有关更多详细信息，见 `管理界面 <https://www.envoyproxy.io/docs/envoy/latest/operations/admin>`_ 。

.. option:: --socket-path <path string>

  *（可选）* :ref:`热重启 <arch_overview_hot_restart>` 套接字地址的输出文件路径。默认为 "@envoy_domain_socket"，它将在抽象名称空间中创建。Suffix _{role}_{id} 被附加以提供名称。希望一起参与热重启的所有 Envoy 进程必须为此选项使用相同的值。

  **注意**：以“@”开头的路径将在抽象名称空间中创建。

.. option:: --socket-mode <string>

  *（可选）* :ref:`热重启 <arch_overview_hot_restart>` 的套接字文件权限。这必须是有效的八进制文件权限，例如 644。默认值为 600。
   当 :option:`--socket-path` 以“@”开头或未设置时，可能不使用该标志。

.. option:: --hot-restart-version

  *（可选）*  为二进制文件输出不透明热重启兼容性版本。可以将其与 :http:get:`/hot_restart_version` 管理端点的输出进行匹配，以确定新二进制文件和正在运行的二进制文件是否与热重启兼容。

.. option:: --service-cluster <string>

  *（可选）* 定义运行 Envoy 的本地服务集群名称。本地服务群集名称首先来自 :ref:`Bootstrap node
  <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` 信息的 :ref:`cluster
  <envoy_v3_api_field_config.core.v3.Node.cluster>` 字段。该 CLI 选项提供了一种指定此值的替代方法，它将覆盖在引导程序配置中设置的任何值。如果使用以下任何功能，则应进行设置：:ref:`statsd <arch_overview_statistics>`、:ref:`健康检查集群验证 <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.service_name_matcher>`、:ref:`运行时覆盖目录 <envoy_v3_api_msg_config.bootstrap.v3.Runtime>`、:ref:`额外的用户代理 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.add_user_agent>`、:ref:`HTTP 全局限流 <config_http_filters_rate_limit>`、 :ref:`CDS <config_cluster_manager_cds>`, and :ref:`HTTP 跟踪 <arch_overview_tracing>`，可以通过此 CLI 选项或在引导程序配置中进行。

.. option:: --service-node <string>

  *（可选）* 定义运行 Envoy 的本地服务节点名称。本地服务节点名称首先来自 :ref:`Bootstrap 节点 <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` 信息的 :ref:`id
  <envoy_v3_api_field_config.core.v3.Node.id>` 字段。此CLI选项提供了一种指定此值的替代方法，它将覆盖在引导程序配置中设置的任何值。 如果使用以下任何功能，则应进行设置：:ref:`statsd <arch_overview_statistics>`、:ref:`CDS <config_cluster_manager_cds>` 和 :ref:`HTTP 跟踪 <arch_overview_tracing>`，可以通过此 CLI 选项或在引导程序配置中进行。

.. option:: --service-zone <string>

  *（可选）* 定义 Envoy 运行所在的本地服务区域。本地服务区域首先来自 :ref:`Bootstrap 节点 <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` 信息的 :ref:`locality.zone <envoy_v3_api_field_config.core.v3.Locality.zone>` 字段。此 CLI 选项提供了一种指定此值的替代方法，它将覆盖在引导程序配置中设置的任何值。如果使用发现服务路由并且发现服务公开了 :ref:`区域数据 <envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` 的情况下需要设置该选项，可以通过此 CLI 选项或在引导程序配置中进行。区域的含义取决于上下文，例如在 AWS 上的 `可用域（Availability Zone AZ） <https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-regions-availability-zones.html>`、 GCP 上的 `域（Zone） <https://cloud.google.com/compute/docs/regions-zones/>`_，等等。


.. option:: --file-flush-interval-msec <integer>

  *（可选）*  文件刷新间隔，以毫秒为单位。默认为 10 秒。在文件创建期间使用此设置来确定缓冲区刷新到文件之间的时间。缓冲区将在每次写满或间隔时间结束时刷新，以先到者为准。当跟踪（tailing） :ref:`访问日志 <arch_overview_access_logs>` 以获取更多（或更少）的立即刷新时，调整此设置很有用。

.. option:: --drain-time-s <integer>

  *（可选）* Envoy 在 :ref:`热重启 <arch_overview_hot_restart>` 期间或通过 :ref:`LDS <arch_overview_dynamic_config_lds>` 修改或删除单个监听器时将排空连接的时间，以秒为单位。 默认为 600秒（10分钟）。通常，排空时间应小于通过 :option:`--parent-shutdown-time-s` 选项设置的父关闭时间。如何配置两个设置取决于特定的部署。在边缘情况下，可能需要很长的排空时间。在服务到服务的方案中，可能使排空和关闭时间大大缩短（例如 60s/90s）。

.. option:: --drain-strategy <string>

  *（可选）* 确定在热重启排空序列期间 Envoy 的行为。在排空序列期间，排空管理器通过在请求完成时终止连接来进行排空，在 HTTP1 上发送“Connection：CLOSE”，在 HTTP2 上发送GOAWAY。

  * ``gradual``：*（默认值）* 随着耗用时间的增加，将要排空的请求百分比增加到 100％。

  * ``immediate``：在排空序列开始后，将所有请求立即排空。

.. option:: --parent-shutdown-time-s <integer>

  *（可选）* 在热重启期间，Envoy 在关闭父进程之前等待的时间，以秒为单位。 有关更多信息，见 :ref:`热重启 <arch_overview_hot_restart>`。 默认为 900 秒（15 分钟）。

.. option:: --disable-hot-restart

  *（可选）* 此标志为启用了该功能的构建禁用 Envoy 热重启。默认情况下，启用热重启。

.. option:: --enable-mutex-tracing

  *（可选）* 此标志启用互斥锁争用统计信息的收集
   （:ref:`MutexStats <envoy_v3_api_msg_admin.v3.MutexStats>`）以及 contention 端点
   （:http:get:`/contention`）。默认情况下不启用互斥量跟踪，因为它会对已经经历互斥量争用的 Envoy 造成轻微的性能损失。

.. option:: --allow-unknown-fields

  *（可选）* :option:`--allow-unknown-static-fields` 的已启用别名。

.. option:: --allow-unknown-static-fields

  *（可选）* 此标志禁用对未知字段的 protobuf 配置的验证。默认情况下，启用验证。对于大多数部署，应使用默认设置，以确保预先捕获配置错误并按预期配置 Envoy。首次使用任何未知字段时都会记录警告，并且这些事件的发生记录统计信息 :ref:`server.static_unknown_fields <server_statistics>` 中。

.. option:: --reject-unknown-dynamic-fields

  *（可选）* 此标志禁用动态配置中未知字段的 protobuf 配置验证。默认情况下，此标志设置为 false，禁用对引导程序意外的字段的验证。这样可以将较新的 xDS 配置交付给较旧的 Envoy。如果不需要这种行为，则可以将其设置为 true 以便进行严格的动态检查，但是对于大多数 Envoy 部署来说，默认值应该是合适的。首次使用任何未知字段时都会记录警告，并且这些发生在 :ref:`server.dynamic_unknown_fields <server_statistics>` 统计信息中进行计数。

.. option:: --ignore-unknown-dynamic-fields

  *（可选）* 此标志禁用动态配置中未知字段的 protobuf 配置验证。 与 :option:`--reject-unknown-dynamic-fields` 设置为false不同，为了提高配置处理速度它不会记录警告或计算未知字段的出现。如果 :option:`--reject-unknown-dynamic-fields` 设置为 true，则此标志无效。

.. option:: --disable-extensions <extension list>

 *（可选）* 此标志禁用逗号分隔的扩展名列表。禁用的扩展无法通过静态或动态配置使用，尽管它们仍链接到 Envoy 并且可能运行启动代码或具有其他运行时效果。扩展名称的创建是通过将扩展类别和名称加上斜线，例如 ``grpc_credentials/envoy.grpc_credentials.file_based_metadata``。

.. option:: --version

 *（可选）* 此标志用于显示 Envoy 版本和构建信息，例如  ``c93f9f6c1e5adddd10a3e3646c7e049c649ae177/1.9.0-dev/Clean/RELEASE/BoringSSL-FIPS``。

  它包含了 5 个用斜线分隔的字段：

  * 源代码版本 - 构建 Envoy 的 git commit，

  * 发行版本 - 发行版本（例如 ``1.9.0``）或者 开发版本（例如 ``1.9.0-dev``），

  * 构建时源代码树的状态 - ``Clean`` 或者 ``Modified``，

  * 构建模式 - ``RELEASE`` 或者 ``DEBUG``，

  * TLS 库 - ``BoringSSL`` 或者 ``BoringSSL-FIPS``。
