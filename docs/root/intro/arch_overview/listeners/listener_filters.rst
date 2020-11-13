.. _arch_overview_listener_filters:

监听器过滤器
================

如 :ref:`监听器 <arch_overview_listeners>` 一节所述, 监听器过滤器可以用于操纵连接元数据。
监听器过滤器的主要目的是更方便地添加系统集成功能，而无需更改 Envoy 核心功能，并使多个此类功能之间的交互更加明确。

监听器过滤器的 API 相对简单，因为最终这些过滤器是在新接收的套接字上操作的。可停止链中的过滤器并继续执行后续的过滤器。这允许去运作更复杂的业务场景，例如调用 :ref:`限速服务 <arch_overview_global_rate_limit>` 等。
Envoy 包含多个监听器过滤器，这些过滤器在架构概述以及 :ref:`配置参考 <config_listener_filters>` 中都有记录。

