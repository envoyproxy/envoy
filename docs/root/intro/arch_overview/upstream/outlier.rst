.. _arch_overview_outlier_detection:

异常点检测
=================

异常点检测和驱逐是指动态判断上游集群中的一些主机是否表现与其他主机不同，并将其从健康的 :ref:`负载均衡 <arch_overview_load_balancing>` 集中移除的过程。性能可能沿着不同的轴线，如连续失败、时间成功率、时间延迟等。异常点检测是一种 *被动* 健康检查的形式。Envoy 还支持 :ref:`主动健康检查 <arch_overview_health_checking>`。*被动* 和 *主动* 健康检查可以一起启用，也可以独立启用，并构成整体上游健康检查解决方案的基础。异常点检测是 :ref:`集群配置 <envoy_v3_api_msg_config.cluster.v3.OutlierDetection>` 的一部分，它需要过滤器来报告错误、超时和重置。目前支持外挂检测的过滤器有：:ref:`HTTP 路由器 <config_http_filters_router>`、:ref:`TCP 代理 <config_network_filters_tcp_proxy>` 和 :ref:`Redis 代理 <config_network_filters_redis_proxy>`。

检测到的错误分为两类：外部产生的错误和本地产生的错误。外部产生的错误是特定的事务，发生在上游服务器上，以响应接收到的请求。例如，HTTP 服务器返回错误代码 500，或 Redis 服务器返回无法解码的有效载荷。这些错误是在 Envoy 成功连接到上游主机后在上游主机上产生的。本地引起的错误是由 Envoy 针对中断或阻止与上游主机通信的事件而产生的。本地错误的例子有超时、TCP 重置、无法连接到指定端口等。

检测到的错误类型取决于过滤器的类型。例如 :ref:`HTTP 路由器 <config_http_filters_router>` 过滤器可以检测本地产生的错误（超时、重置 — 与上游主机连接有关的错误），由于它也理解 HTTP 协议，所以它报告 HTTP 服务器返回的错误（外部产生的错误）。在这种情况下，即使与上游 HTTP 服务器的连接成功，与服务器的交易也可能失败。相比之下，:ref:`TCP 代理 <config_network_filters_tcp_proxy>` 过滤器不理解 TCP 层以上的任何协议，只报告本地产生的错误。

在默认配置下（:ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *false*），本地产生的错误与外部产生的（事务）错误并不区分，都会被归入同一个桶，并与 :ref:`outlier_detection.consecutive_5xx <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>`, :ref:`outlier_detection.consecutive_gateway_failure <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_gateway_failure>` 和 :ref:`outlier_detection.success_rate_stdev_factor <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_stdev_factor>` 配置项。例如，如果与上游 HTTP 服务器的连接因为超时而导致两次失败，在连接建立成功后，服务器返回错误代码 500，那么总的错误数将为 3。

异常点检测也可以被配置为区分本地来源的错误和外部来源的（事务）错误。它是通过 :ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 配置项完成的。在该模式下，本地来源的错误与外部来源的（事务）错误由不同的计数器跟踪，异常点检测器可以配置为对本地来源的错误做出反应，而忽略外部来源的错误，反之亦然。

重要的是要理解，一个集群可能由几个过滤链共享。如果一个过滤链根据它的异常点检测类型驱逐一个主机，其他过滤链也会受到影响，即使它们的异常点检测类型不会驱逐该主机。

驱逐算法
------------------

根据异常点检测的类型，驱逐要么在线运行（例如在连续 5xx 的情况下），要么以指定的时间间隔（例如在周期性成功率的情况下）。驱逐算法的工作原理如下：

#. 一个主机被认定为是异常点。
#. 如果没有主机被驱逐，Envoy 将立即驱逐主机。否则，它会检查是否驱逐的主机数量低于允许的阈值（通过 :ref:`outlier_detection.max_ejection_percent <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_percent>` 设置指定）。如果被驱逐的主机数量高于阈值，则不驱逐该主机。
#. 主机被驱逐若干毫秒。驱逐意味着主机被标记为不健康，并且在负载均衡期间不会被使用，除非负载均衡器处于 :ref:`异常 <arch_overview_load_balancing_panic_threshold>` 场景。毫秒数等于 :ref:`outlier_detection.base_ejection_time_ms <envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` 值乘以主机被驱逐的次数。这就导致主机如果继续失败，被驱逐的时间会越来越长。
#. 被驱逐的主机在满足驱逐时间后会自动恢复服务。一般来说，异常点检测与 :ref:`主动健康检查 <arch_overview_health_checking>` 一起使用，是一个全面的健康检查解决方案。

检测类型
---------------

Envoy 支持以下异常点检测类型。


连续 5xx
^^^^^^^^^^^^^^^

在默认模式下（:ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *false*），该检测类型会考虑所有产生的错误：本地产生的和外部产生的（事务）错误。由非 HTTP 过滤器产生的错误，如 :ref:`TCP 代理 <config_network_filters_tcp_proxy>` 或 :ref:`Redis 代理 <config_network_filters_redis_proxy>` 在内部被映射到 HTTP 5xx 代码，并被如此处理。

在分裂模式下（:ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *true*），该检测类型只考虑外部来源的（事务）错误，忽略本地来源的错误。如果上游主机是 HTTP 服务器，则只考虑 5xx 类型的错误（参见 :ref:`连续网关故障 <consecutive_gateway_failure>` 了解例外情况）。对于 Redis 服务器，通过 :ref:`Redis 代理 <config_network_filters_redis_proxy>` 服务的，只考虑服务器的错误响应。正确格式化的响应，即使是带有操作错误的响应（如索引未找到，访问被拒绝）也不会被考虑。

如果上游主机返回一些被视为连续 5xx 类型错误的错误数量，它将被弹出。弹出所需的连续 5xx 数量由 :ref:`outlier_detection.consecutive_5xx <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>` 值控制。

.. _consecutive_gateway_failure:

连续网关故障
^^^^^^^^^^^^^^^^^^^^^^^^^^^

这种检测类型考虑到 5xx 错误的子集，称为“网关错误”（502、503 或 504 状态码），仅由 :ref:`HTTP 路由器 <config_http_filters_router>` 支持。

如果上游主机连续返回一定数量的“网关错误”（502、503 或 504 状态码），就会被驱逐。驱逐所需的连续网关故障次数由 :ref:`outlier_detection.consecutive_gateway_failure <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_gateway_failure>` 值控制。

连续本地源失败
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

只有当 :ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *true* 时，才会启用该检测类型，并且只考虑本地来源的错误（超时、重置等）。如果 Envoy 反复无法连接到上游主机，或者与上游主机的通信反复中断，将被驱逐。检测到各种本地发起的问题：超时、TCP 重置、ICMP 错误等。驱逐所需的连续本地起源故障次数由 :ref:`outlier_detection.consecutive_local_origin_failure <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_local_origin_failure>` 值控制。该检测类型由 :ref:`HTTP 路由器 <config_http_filters_router>`、:ref:`TCP 代理 <config_network_filters_tcp_proxy>` 和 :ref:`Redis 代理 <config_network_filters_redis_proxy>` 支持。

成功率
^^^^^^^^^^^^

基于成功率的异常点检测，将集群中每台主机的成功率数据汇总。然后在给定的时间间隔内，根据统计的异常点检测来驱逐主机。如果一台主机在聚合区间内的请求量小于 :ref:`outlier_detection.success_rate_request_volume <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>` 值，则不会对该主机进行检测。此外，如果在一个区间内具有最小要求请求量的主机数量小于  :ref:`outlier_detection.success_rate_minimum_hosts <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_minimum_hosts>` 值，则不会对集群进行检测。 

在默认的配置模式下（:ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *false*），这个检测类型会考虑所有类型的错误：本地和外部来源的。:ref:`outlier_detection.enforcing_local_origin_success <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_local_origin_success_rate>` 配置项被忽略。

在拆分模式下（:ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` 为 *true*），本地发起的错误和外部发起的（事务）错误会被分别计算和处理。大多数配置项，即 :ref:`outlier_detection.success_rate_minimum_hosts <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_minimum_hosts>`、:ref:`outlier_detection.success_rate_request_volume <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>`, :ref:`outlier_detection.success_rate_stdev_factor <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_stdev_factor>` 适用于两种类型的错误，但 :ref:`outlier_detection.enforcing_success_rate <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_success_rate>` 只适用于外部来源的错误，而 :ref:`outlier_detection. enforcing_local_origin_success_rate <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_local_origin_success_rate>` 只适用于本地产生的错误。

.. _arch_overview_outlier_detection_failure_percentage:

故障百分比
^^^^^^^^^^^^^^^^^^

基于故障百分比的异常点检测功能与成功率检测类似，因为它依赖于集群中每个主机的成功率数据。然而，它不是将这些值与整个集群的平均成功率进行比较，而是与用户配置的统一阈值进行比较。这个阈值是通过 :ref:`outlier_detection.failure_percentage_threshold <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_threshold>` 字段配置的。

基于失败百分比检测的其他配置字段与成功率检测的字段类似。基于失败百分比的检测也服从 :ref:`outlier_detection.split_external_local_origin_errors <envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>`；外部和本地来源的错误的执行百分比由 :ref:`outlier_detection.enforcing_failure_percentage <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage>` 和 :ref:`outlier_detection.enforcing_failure_percentage_local_origin <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage_local_origin>` 分别控制。与成功率检测一样，如果一台主机在聚合区间内的请求量小于 :ref:`outlier_detection.failure_percentage_request_volume <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_request_volume>` 值，则不会对其进行检测。如果一个群集在一个区间内所需的最小请求量的主机数量小于 :ref:`outlier_detection.failure_percentage_minimum_hosts <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_minimum_hosts>` 值，也不会进行检测。

.. _arch_overview_outlier_detection_grpc:

gRPC
----------------------

对于 gRPC 请求，异常点检测将使用从 `grpc-status <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses>`_ 响应头映射的 HTTP 状态。


.. _arch_overview_outlier_detection_logging:

驱逐事件记录
----------------------

Envoy 可以选择生成异常点驱逐事件的日志。这在日常操作中非常有用，因为全局统计不能提供足够的信息，说明哪些主机被驱逐，以及驱逐的原因。日志的结构是基于 Protobuf 的转储 :ref:`OutlierDetectionEvent 消息 <envoy_v3_api_msg_data.cluster.v3.OutlierDetectionEvent>`。驱逐事件日志配置在集群管理器 :ref:`异常点检测配置 <envoy_v3_api_field_config.bootstrap.v3.ClusterManager.outlier_detection>`。

配置参考
-----------------------

* 集群管理器 :ref:`全局配置 <envoy_v3_api_field_config.bootstrap.v3.ClusterManager.outlier_detection>`
* 单个集群 :ref:`配置 <envoy_v3_api_msg_config.cluster.v3.OutlierDetection>`
* 运行时 :ref:`设置 <config_cluster_manager_cluster_runtime_outlier_detection>`
* 统计 :ref:`参考 <config_cluster_manager_cluster_stats_outlier_detection>`
