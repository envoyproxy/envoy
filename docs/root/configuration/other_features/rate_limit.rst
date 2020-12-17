.. _config_rate_limit_service:

限流服务
=========

:ref:`限流服务 <arch_overview_global_rate_limit>` 配置指定了全局限流服务，当 Envoy 需要对全局限流服务做决定的时候，会与其进行通信。如果没有配置限流服务，则将使用一个在调用时永远返回 OK 的 "null" 服务。


* :ref:`v3 API 参考 <envoy_v3_api_msg_config.ratelimit.v3.RateLimitServiceConfig>`

gRPC 服务 IDL
--------------

Envoy 期望限流服务能够支持在 :ref:`rls.proto <envoy_v3_api_file_envoy/service/ratelimit/v3/rls.proto>` 中指定的 gRPC IDL（Interactive Data Language 互动式数据语言）。更多关于 API 是如何工作的信息，可以查看 IDL 文档。在 `这儿 <https://github.com/lyft/ratelimit>`_ 查看 Lyft 的配置实现。
