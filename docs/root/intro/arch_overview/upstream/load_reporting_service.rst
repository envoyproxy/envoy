.. _arch_overview_load_reporting_service:

负载上报服务 (LRS)
============================

负载上报服务提供了一种机制，使 Envoy 可以定期向管理服务器发送负载报告。

这将启动一个与管理服务器的双向流。连接后，管理服务器可以发送一个 :ref:`LoadStatsResponse <envoy_v3_api_msg_service.load_stats.v3.LoadStatsResponse>` 给它有兴趣获取负载报告的节点。这个节点中的 Envoy 将开始发送 :ref:`LoadStatsRequest <envoy_v3_api_msg_service.load_stats.v3.LoadStatsRequest>`。这是根据 :ref:`负载上报时间间隔 <envoy_v3_api_field_service.load_stats.v3.LoadStatsResponse.load_reporting_interval>` 定期进行的。

负载上报服务的配置可以参考 :repo:`/examples/load-reporting-service/service-envoy-w-lrs.yaml`。

