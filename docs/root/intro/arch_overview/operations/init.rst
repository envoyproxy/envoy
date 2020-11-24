.. _arch_overview_initialization:

初始化
==============

Envoy 在启动时的初始化是很复杂的。本章将从高层次上解释该过程是如何工作的。在所有监听器启动监听并接收新连接之前，会发生以下所有过程。

* 启动期间，:ref:`集群管理器 <arch_overview_cluster_manager>` 会经过一个多阶段的初始化，首先初始化静态/DNS 集群，然后初始化预定义的 :ref:`EDS <arch_overview_dynamic_config_eds>` 集群。如果适用，它将初始化 :ref:`CDS <arch_overview_dynamic_config_cds>`，等待响应（或失败） :ref:`一段有限的时间 <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>` ， 并为 CDS 提供的集群执行相同主/次初始化。
* 如果集群使用 :ref:`主动健康检查 <arch_overview_health_checking>`，Envoy 也会执行一次主动健康检查。
* 集群管理器初始化完成后，:ref:`RDS <arch_overview_dynamic_config_rds>` 和 :ref:`LDS <arch_overview_dynamic_config_lds>` 将进行初始化（如果适用）。服务器将等待 :ref:`一段有限的时间 <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>` ，来接受至少一个 LDS/RDS 请求的响应（或者失败）后，服务器才开始接受连接。
* 如果 LDS 本身返回需要 RDS 响应的监听器，则 Envoy 会进一步等待 :ref:`一段有限的时间 <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`，直到接收到 RDS 响应（或失败）为止。请注意，这个过程会在后续每个通过 LDS 添加的监听器进行，称为 :ref:`监听器热身 <config_listeners_lds>`。
* 在完成前面的所有步骤之后，监听器开始接受新的连接。该流程可确保在热重启期间，新进程完全能够在旧进程被驱逐之前接受并处理新连接。

初始化的关键设计原则是，始终确保 Envoy 在 :ref:`初始化获取超时 <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>` 时间段内完成初始化，并在管理服务器可用性的前提下，尽最大努力获得完整的 xDS 配置集。

