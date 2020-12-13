.. _config_health_checkers_redis:

Redis
=====

Redis 健康检查器是一个用于检查 Redis 上游主机的定制健康检查器（名为 :code:`envoy.health_checkers.redis`）。通过发送 Redis PING 命令并期望 PONG 响应进行工作。上游 Redis 服务器可以通过 PONG 以外的任何响应来立即导致运行健康检查失败。或者，Envoy 可以在用户指定的密钥上执行 EXISTS。如果密钥不存在，则将其视为通过健康检查。这允许用户通过将指定的 :ref:`密钥 <envoy_v3_api_field_config.health_checker.redis.v2.Redis.key>` 设置为任意值并等待流量排空来标记 Redis 实例以进行维护。

:ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>` 作为 Redis 运行状况健康检查器的一个配置示例如下:

.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
        key: foo

* :ref:`v3 API 参考 <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
