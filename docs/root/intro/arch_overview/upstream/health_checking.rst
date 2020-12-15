.. _arch_overview_health_checking:

健康检查
===============

可以在每个上游集群的基础上 :ref:`配置 <config_cluster_manager_cluster_hc>` 主动健康检查。如 :ref:`服务发现 <arch_overview_service_discovery>` 部分所述，主动健康检查和 EDS 服务发现类型是并行协作的。 但是，在其他情况下，即使使用其他服务发现类型，也期望有主动健康检查。Envoy 支持三种不同类型的运行健康检查以及各种设置（检查时间间隔、主机不健康标记为故障、主机健康时标记为成功等）：

* **HTTP**: 在 HTTP 运行健康检查期间，Envoy 将向上游主机发送HTTP请求。 默认情况下，如果主机运行状况良好，则期望 200 响应。 预期的响应代码是 :ref:`可配置 <envoy_v3_api_msg_config.core.v3.HealthCheck.HttpHealthCheck>` 的。 如果上游主机希望立即通知下游主机不再向其转发流量，则可以返回 503 。
* **L3/L4**: 在 L3/L4 健康检查期间，Envoy 会向上游主机发送一个可配置的字节缓冲区。如果主机被认为是健康的，字节缓冲区在响应中会被显示出来。Envoy 还支持仅连接 L3/L4 健康检查。
* **Redis**: Envoy 将发送 Redis PING 命令并期望 PONG 响应。如果上游 Redis 服务器使用 PONG 以外的任何其他响应命令，则会导致健康检查失败。或者，Envoy 可以在用户指定的键上执行 EXISTS。如果键不存在，则认为它是合格的健康检查。这允许用户通过将指定的键设置为任意值来标记 Redis 实例以进行维护直至流量耗尽。请参阅 :ref:`redis_key <envoy_v3_api_msg_config.health_checker.redis.v2.Redis>` 。

健康检查是运行在为集群指定的传输套接字之上的。这意味着，如果集群使用启用了 TLS 的传输套接字，则健康检查也将在 TLS 之上运行。 可以指定用于健康检查连接的 :ref:`TLS 选项 <envoy_v3_api_msg_config.core.v3.HealthCheck.TlsOptions>` ，如果对应的上游使用基于 ALPN 的 :ref:FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>，健康检查与数据连接使用不同的协议，这将是非常有用的。

.. _arch_overview_per_cluster_health_check_config:

每个集群成员健康检查配置
-------------------------

如果为上游集群配置了主动健康检查，则可以通过在 :ref:`ClusterLoadAssignment<envoy_v3_api_msg_config.endpoint.v3.ClusterLoadAssignment>` 中每个已定义的 :ref:`LocalityLbEndpoints<envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` 的 :ref:`LbEndpoint<envoy_v3_api_msg_config.endpoint.v3.LbEndpoint>` 的 :ref:`Endpoint<envoy_v3_api_msg_config.endpoint.v3.Endpoint>` 中设置 :ref:`HealthCheckConfig<envoy_v3_api_msg_config.endpoint.v3.Endpoint.HealthCheckConfig>` 来为每个注册成员指定特定的附加配置。

以下示例为设置运行 :ref:`健康检查配置<envoy_v3_api_msg_config.endpoint.v3.Endpoint.HealthCheckConfig>` 以设置 :ref:`集群成员<envoy_v3_api_msg_config.endpoint.v3.Endpoint>` 可选运行健康检查的 :ref:`端口<envoy_v3_api_field_config.endpoint.v3.Endpoint.HealthCheckConfig.port_value>` 

.. code-block:: yaml

  load_assignment:
    endpoints:
    - lb_endpoints:
      - endpoint:
          health_check_config:
            port_value: 8080
          address:
            socket_address:
              address: localhost
              port_value: 80

.. _arch_overview_health_check_logging:

健康检查事件日志
-----------------
Envoy 可以通过在 :ref:`HealthCheck 配置 <envoy_v3_api_field_config.core.v3.HealthCheck.event_log_path>` 中指定日志文件路径，选择性地生成包含弹出和添加事件的 per-healthchecker 日志。日志结构为 :ref:`HealthCheckEvent 消息 <envoy_v3_api_msg_data.core.v3.HealthCheckEvent>` 的 JSON dumps。

通过将 :ref:`always_log_health_check_failures
标志 <envoy_v3_api_field_config.core.v3.HealthCheck.always_log_health_check_failures>` 设置为 true，来配置 Envoy 以记录所有健康检查失败事件。

被动的健康检查
----------------
Envoy 还支持通过 :ref:`异常检测
<arch_overview_outlier_detection>` 进行被动健康检查。


连接池交互
------------

请参阅 :ref:`此处 <arch_overview_conn_pool_health_checking>` 了解更多信息。

.. _arch_overview_health_checking_filter:

HTTP 健康检查过滤器
---------------------------

当部署 Envoy 网格并在集群之间进行主动健康检查时，会生成大量健康检查流量。Envoy 包含一个 HTTP 健康检查过滤器，可以安装在配置的 HTTP 监听器中。这个过滤器有几种不同的操作模式：

* **不通过**: 健康检查请求永远不会被传递给本地服务。Envoy 会根据当前服务器的排空状态来返回 200 或 503。
* **不通过，根据上游集群健康状况计算**: 在此模式下，运行健康检查过滤器将返回 200 或 503，具体取决于一个或多个上游集群中是否至少有 :ref:`指定百分比 <envoy_v3_api_field_extensions.filters.http.health_check.v3.HealthCheck.cluster_min_healthy_percentages>` 的服务器可用(运行状况+降级)。(但是，如果 Envoy 服务器处于排空状态，则无论上游集群运行状况如何，它都将使用 503 响应。)  

* **通过**: 在此模式下，Envoy 会将每个健康检查请求传递给本地服务。根据该服务的健康状态返回 200 或 503。

* **通过缓存传递**: 在此模式下，Envoy 会将健康检查请求传递给本地服务，但会将结果缓存一段时间。在缓存有效期内，随后的健康检查请求会直接返回从缓存的获取的值。缓存过期后，后续的健康检查请求将传递给本地服务。操作大型网格时，推荐使用此操作模式。Envoy 会保持健康检查的连接，所以健康检查请求对 Envoy 自身的耗费很小。因此，这种操作模式对每个上游主机的健康状态生成了最终一致的视图，而没有用大量的健康检查请求压倒本地服务。

进一步阅读: 

* 健康检查过滤器 :ref:`配置 <config_http_filters_health_check>`。
* :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` 管理端点。
* :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` 管理端点。

主动健康检查快速失败
----------------------

在使用主动健康检查和被动健康检查( :ref:`异常检测
<arch_overview_outlier_detection>` )时，通常使用较长的运行健康检查间隔来避免大量的主动健康检查流量。在这种情况下，当使用 :ref:`x-envoy-immediate-health-check-fail
<config_http_filters_router_x-envoy-immediate-health-check-fail>` 管理端点来尽快排空上游主机仍旧是非常有效的手段。为了支持这一点， :ref:`路由器过滤器 <config_http_filters_router>` 将响应 :ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>` 头。如果上游主机设置了此头，Envoy 会立即将该主机标记为主动健康检查失败。注意，只有在主机集群 :ref:`配置
<config_cluster_manager_cluster_hc>` 了主动健康检查时才会发生这种情况。如果通过 :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` 管理端点将 Envoy 标记为失败，则 :ref:`健康检查筛选器<config_http_filters_health_check>` 将自动设置此头。

.. _arch_overview_health_checking_identity:

健康检查识别
------------

只验证上游主机是否响应特定的健康检查 URL 并不一定意味着上游主机有效。例如，当在自动扩缩容的云环境或容器环境中使用最终一致的服务发现时，主机可能会消失，但随后其他主机以相同的 IP 地址返回，这是有可能的。解决此问题的一个办法是针对每种服务类型都有不同的 HTTP 健康检查 URL。该方法的缺点是整体配置会变得更加复杂，因为每个健康检查 URL 都是完全自定义的。

Envoy HTTP 健康检查器支持 :ref:`service_name_matcher
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.service_name_matcher>` 选项。如果设置了此选项，健康检查程序还会将 *x-envoy-upstream-healthchecked-cluster* 
响应头部的值与 *service_name_matcher* 进行比较。如果值不匹配，则健康检查不通过。上游健康检查过滤器会将 *x-envoy-upstream-healthchecked-cluster* 附加到响应头。这个值由 :option:`--service-cluster` 命令行选项决定。

.. _arch_overview_health_checking_degraded:

健康状况下降
---------------
使用 HTTP 健康检查器时，上游主机可以返回 ``x-envoy-degraded`` 以通知健康检查器该主机已降级。 请参阅 :ref:`此处 <arch_overview_load_balancing_degraded>` 以了解这如何影响负载均衡。
